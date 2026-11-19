/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file mam_sync.c XEP-0313 catch-up of one archive (ARCHITECTURE 7.2)
 *
 *   IDLE --begin--> SYNCING --fin complete--> LIVE
 *                     |  ^ fin incomplete: next page (after=<last>)
 *                     |  item-not-found on after=cursor: retry once
 *                     |  with start=cursor_ts
 *                     +--error, watchdog, teardown--> LIVE
 *
 * One instance per archive: the account archive (begins in pre_presence;
 * the first query waits for session_ready and, when the server offers
 * carbons and the carbons module is registered, for
 * x2_ext_carbons_settled()) and each joined MAM room (begins and queries on
 * the join, see mam_muc.c).
 */
#include "xmpp2_internal.h"

#include <stdlib.h>

#include "conversation.h"
#include "debug.h"
#include "eventloop.h"
#include "notify.h"

#include "core/mam_proto.h"
#include "core/sid.h"
#include "ext/mam.h"
#include "iq.h"

/* Test hook: build and log the query but never send it, so the watchdog
 * fires (tests/integration/test_mam_recovery.sh). */
#define X2_MAM_STALL_ENV "XMPP2_TEST_MAM_STALL"

static X2Store *
store(X2MamArchive *a)
{
	return x2_mam_store(a->st);
}

/* For the log: "account archive" or "room archive <jid>" */
static const char *
label(X2MamArchive *a)
{
	return a->room ? "room archive " : "account archive";
}

static const char *
label_jid(X2MamArchive *a)
{
	return a->room ? a->jid : "";
}

X2MamArchive *
x2_mam_archive_new(X2MamState *st, const char *jid, gboolean room)
{
	X2MamArchive *a = g_new0(X2MamArchive, 1);

	a->st = st;
	a->jid = g_strdup(jid);
	a->room = room;
	a->gap_noticed = g_hash_table_new_full(g_str_hash, g_str_equal,
	                                       g_free, NULL);
	g_queue_init(&a->queue);
	g_queue_init(&a->page);
	return a;
}

void
x2_mam_archive_free(X2MamArchive *a)
{
	if (a == NULL)
		return;
	x2_mam_sync_reset(a);
	g_hash_table_destroy(a->gap_noticed);
	g_free(a->jid);
	g_free(a);
}

static gboolean
watchdog_cb(gpointer data)
{
	X2MamArchive *a = data;
	X2MamState *st = a->st;

	a->watchdog = 0;
	if (a->phase != X2_MAM_SYNCING)
		return FALSE;
	if (a == st->acct && a->queryid == NULL && st->ready) {
		purple_debug_warning(X2_MAM_DEBUG, "carbons enable unanswered, "
		                     "querying the archive anyway\n");
		st->carbons_settled = TRUE;
		x2_mam_sync_maybe_start(st);
		return FALSE;
	}
	purple_debug_warning(X2_MAM_DEBUG, "watchdog: no %s%s answer in %d s\n",
	                     label(a), label_jid(a), X2_MAM_WATCHDOG_SECS);
	x2_mam_sync_finish(a, FALSE,
	                   _("Message history may be incomplete: the server "
	                     "did not answer the history request in time."));
	return FALSE;
}

void
x2_mam_sync_kick(X2MamArchive *a)
{
	if (a->watchdog != 0)
		purple_timeout_remove(a->watchdog);
	a->watchdog = 0;
	if (a->phase == X2_MAM_SYNCING)
		a->watchdog = purple_timeout_add_seconds(X2_MAM_WATCHDOG_SECS,
		                                         watchdog_cb, a);
}

void
x2_mam_sync_begin(X2MamArchive *a)
{
	x2_mam_sync_reset(a);
	a->pages = 0;
	a->from_cursor = FALSE;
	a->retried = FALSE;
	a->capped = FALSE;
	a->frozen = FALSE;
	g_hash_table_remove_all(a->gap_noticed);
	purple_debug_info(X2_MAM_DEBUG, "%s%s SYNCING\n", label(a), label_jid(a));
	a->phase = X2_MAM_SYNCING;
	x2_mam_sync_kick(a);
}

/* No usable cursor: the first-run rule (ARCHITECTURE 7.2) */
static void
first_run(X2MamArchive *a)
{
	PurpleAccount *account;
	int days;

	if (a->room) {
		x2_mam_sync_query(a, X2_MAMQ_LAST, NULL, 0, NULL);
		return;
	}
	account = purple_connection_get_account(a->st->js->gc);
	days = purple_account_get_int(account, "mam_initial_days", 1);
	if (x2_store_cursor_only(store(a)) || days <= 0)
		x2_mam_sync_query(a, X2_MAMQ_CURSOR_ONLY, NULL, 0, NULL);
	else
		x2_mam_sync_query(a, X2_MAMQ_FORWARD, NULL,
		                  time(NULL) - (time_t)days * 24 * 3600, NULL);
}

void
x2_mam_sync_maybe_start(X2MamState *st)
{
	X2MamArchive *a = st->acct;
	const char *cursor;

	if (a->phase != X2_MAM_SYNCING || !st->ready || a->queryid != NULL)
		return;
	if (st->carbons_offered && !st->carbons_settled &&
	    x2_ext_registered("carbons")) {
		purple_debug_info(X2_MAM_DEBUG, "waiting for carbons enable\n");
		return;
	}

	cursor = x2_store_get_cursor(store(a), a->jid, NULL);
	a->from_cursor = cursor != NULL;
	if (cursor != NULL)
		x2_mam_sync_query(a, X2_MAMQ_FORWARD, cursor, 0, NULL);
	else
		first_run(a);
}

/* item-not-found on after=cursor: the cursor left the archive (or is
 * corrupt).  Retry once from its timestamp, else apply the first-run rule. */
static void
retry_from_ts(X2MamArchive *a)
{
	gint64 ts = 0;
	const char *cursor = x2_store_get_cursor(store(a), a->jid, &ts);

	a->retried = TRUE;
	a->from_cursor = FALSE;
	a->pages = 0;
	if (cursor != NULL && ts > 0) {
		purple_debug_info(X2_MAM_DEBUG, "cursor %s unknown to the "
		                  "archive, retrying from %" G_GINT64_FORMAT "\n",
		                  cursor, ts);
		g_free(a->skip_id);
		a->skip_id = g_strdup(cursor);
		x2_mam_sync_query(a, X2_MAMQ_FORWARD, NULL, (time_t)ts, NULL);
	} else {
		purple_debug_info(X2_MAM_DEBUG, "cursor unknown to the archive, "
		                  "no timestamp: first-run rule\n");
		first_run(a);
	}
}

/* Index of the message with stanza-id @a id in the held page, or -1 */
static int
page_index(X2MamArchive *a, const char *id)
{
	GList *l;
	int i = 0;

	if (id == NULL)
		return -1;
	for (l = a->page.head; l != NULL; l = l->next, i++)
		if (purple_strequal(((X2Msg *)l->data)->stanza_id, id))
			return i;
	return -1;
}

/*
 * Delivers the held results of a last-page query.  Messages up to the
 * cursor were shown before (a room whose cursor is older than the
 * threshold, or a page cap that overlaps the pages already shown): drop
 * them, and then nothing was skipped.  Otherwise, if @a skipped, write
 * "older history skipped" before the first message of each conversation.
 */
static void
flush_page(X2MamArchive *a, gboolean skipped)
{
	int seen = page_index(a, x2_store_get_cursor(store(a), a->jid, NULL));
	X2Msg *d;

	if (seen >= 0) {
		purple_debug_info(X2_MAM_DEBUG, "last page reaches the cursor: "
		                  "%d already shown\n", seen + 1);
		skipped = FALSE;
		for (; seen >= 0; seen--)
			x2_msg_free(g_queue_pop_head(&a->page));
	}
	while ((d = g_queue_pop_head(&a->page)) != NULL) {
		if (x2_mam_key_seen(a, d->key)) {
			x2_msg_free(d);
			continue;
		}
		if (skipped && d->peer != NULL &&
		    g_hash_table_add(a->gap_noticed,
		                     g_strdup(a->room ? a->jid : d->peer)))
			x2_mam_notice(a, d->peer, _("Older history skipped."));
		x2_mam_remember(a, d);
		x2_msg_redeliver(a->st->js, d);
	}
}

static void
query_cb(JabberStream *js, const char *from, JabberIqType type,
         const char *id, xmlnode *packet, gpointer data)
{
	X2MamArchive *a = data;
	X2MamFin fin;
	guint max_pages = a->room ? X2_MAM_ROOM_MAX_PAGES : X2_MAM_MAX_PAGES;

	/* The archive may have moved on (watchdog, rejoin, newer query) */
	if (a->phase != X2_MAM_SYNCING || !purple_strequal(id, a->iq_id))
		return;

	if (type == JABBER_IQ_ERROR) {
		if (x2_mam_error_is_item_not_found(packet) && !a->retried &&
		    a->pages == 0 && a->kind == X2_MAMQ_FORWARD &&
		    x2_store_get_cursor(store(a), a->jid, NULL) != NULL) {
			retry_from_ts(a);
			return;
		}
		purple_debug_error(X2_MAM_DEBUG, "archive query failed\n");
		x2_mam_sync_finish(a, FALSE,
		                   _("Message history may be incomplete: the "
		                     "server refused the history request."));
		return;
	}

	if (!x2_mam_fin_parse(packet, &fin)) {
		purple_debug_error(X2_MAM_DEBUG, "archive answer without <fin/>\n");
		x2_mam_sync_finish(a, FALSE, NULL);
		return;
	}

	a->pages++;
	purple_debug_info(X2_MAM_DEBUG, "MAM page %u: complete=%s last=%s\n",
	                  a->pages, fin.complete ? "true" : "false",
	                  fin.last ? fin.last : "(none)");

	if (a->kind == X2_MAMQ_PROBE) {
		if (fin.last != NULL)
			x2_mam_sync_finish(a, TRUE, NULL);
		else
			retry_from_ts(a);
		x2_mam_fin_clear(&fin);
		return;
	}

	/* Some archives (Prosody's internal storage) answer an unknown
	 * after=<id> with an empty page instead of item-not-found: make sure
	 * an empty answer means "nothing new" and not "cursor gone". */
	if (a->from_cursor && a->pages == 1 && fin.last == NULL &&
	    a->extended && !a->retried) {
		x2_mam_sync_query(a, X2_MAMQ_PROBE, NULL, 0,
		                  x2_store_get_cursor(store(a), a->jid, NULL));
		x2_mam_fin_clear(&fin);
		return;
	}

	if (a->kind == X2_MAMQ_LAST)
		flush_page(a, a->capped || !fin.complete);

	if (fin.last != NULL) {
		x2_store_set_cursor(store(a), a->jid, fin.last,
		                    a->page_ts ? (gint64)a->page_ts
		                               : (gint64)time(NULL));
		x2_statefile_touch(a->st->sf);
	}

	if (fin.complete || fin.last == NULL || a->kind != X2_MAMQ_FORWARD)
		x2_mam_sync_finish(a, TRUE, NULL);
	else if (a->pages >= max_pages - 1) {
		purple_debug_info(X2_MAM_DEBUG, "page cap: jumping to the "
		                  "newest page\n");
		a->capped = TRUE;
		x2_mam_sync_query(a, X2_MAMQ_LAST, NULL, 0, NULL);
	} else
		x2_mam_sync_query(a, X2_MAMQ_FORWARD, fin.last, 0, NULL);

	x2_mam_fin_clear(&fin);
}

static guint
page_size(X2MamArchive *a, X2MamQueryKind kind)
{
	if (kind == X2_MAMQ_CURSOR_ONLY || kind == X2_MAMQ_PROBE)
		return 1;
	return a->room ? X2_MAM_ROOM_PAGE_SIZE : X2_MAM_PAGE_SIZE;
}

void
x2_mam_sync_query(X2MamArchive *a, X2MamQueryKind kind, const char *after,
                  time_t start, const char *id)
{
	X2MamQuery q = { 0 };
	JabberIq *iq;
	xmlnode *query;
	char *queryid = x2_sid_new_id();

	q.queryid = queryid;
	q.after = after;
	q.start = start;
	q.id = id;
	q.last_page = kind == X2_MAMQ_LAST || kind == X2_MAMQ_CURSOR_ONLY;
	q.max = page_size(a, kind);
	query = x2_mam_query_new(&q);
	if (query == NULL) {
		g_free(queryid);
		x2_mam_sync_finish(a, FALSE, NULL);
		return;
	}

	iq = jabber_iq_new(a->st->js, JABBER_IQ_SET);
	if (a->room)
		xmlnode_set_attrib(iq->node, "to", a->jid);
	xmlnode_insert_child(iq->node, query);
	/* a lives as long as the stream (rooms are never freed before) */
	jabber_iq_set_callback(iq, query_cb, a);

	g_free(a->queryid);
	a->queryid = queryid;
	g_free(a->iq_id);
	a->iq_id = g_strdup(iq->id);
	a->kind = kind;
	a->page_ts = 0;
	x2_mam_sync_kick(a);

	purple_debug_info(X2_MAM_DEBUG, "MAM query %s%s%s: %s after=%s "
	                  "start=%ld\n", a->iq_id, a->room ? " to " : "",
	                  label_jid(a),
	                  kind == X2_MAMQ_FORWARD ? "forward" :
	                  kind == X2_MAMQ_LAST ? "last-page" :
	                  kind == X2_MAMQ_PROBE ? "probe" : "cursor-only",
	                  after ? after : "-", (long)start);

	if (g_getenv(X2_MAM_STALL_ENV) != NULL) {
		purple_debug_warning(X2_MAM_DEBUG, "test hook: query not sent\n");
		jabber_iq_free(iq);
		return;
	}
	jabber_iq_send(iq);
}

void
x2_mam_sync_finish(X2MamArchive *a, gboolean complete, const char *notice)
{
	X2Msg *d;

	if (a->watchdog != 0)
		purple_timeout_remove(a->watchdog);
	a->watchdog = 0;
	g_free(a->queryid);
	a->queryid = NULL;
	g_free(a->iq_id);
	a->iq_id = NULL;
	g_free(a->skip_id);
	a->skip_id = NULL;
	a->phase = X2_MAM_LIVE;
	if (!complete)
		a->frozen = TRUE;

	purple_debug_info(X2_MAM_DEBUG, "%s%s LIVE (%s), %u queued\n",
	                  label(a), label_jid(a),
	                  complete ? "complete" : "incomplete",
	                  g_queue_get_length(&a->queue));

	/* An interrupted last page: show what came, the notice says why */
	flush_page(a, FALSE);
	/* One notice for the archive, even when nothing was queued: into the
	 * room, or account-level for the account archive (the conversations
	 * in the queue also get a line below). */
	if (notice != NULL && a->room)
		x2_mam_notice(a, NULL, notice);
	else if (notice != NULL)
		purple_notify_message(a->st->js->gc, PURPLE_NOTIFY_MSG_WARNING,
		                      _("Message history"), notice, NULL, NULL,
		                      NULL);

	/* Delivered in arrival order; dedup again, the catch-up may have
	 * shown some of them already. */
	g_hash_table_remove_all(a->gap_noticed);
	while ((d = g_queue_pop_head(&a->queue)) != NULL) {
		switch (x2_mam_queued_fate(x2_mam_key_seen(a, d->key), complete,
		                           a->kind == X2_MAMQ_LAST, d->delayed,
		                           d->stanza_id != NULL)) {
		case X2_MAM_QUEUED_DUPLICATE:
			purple_debug_info(X2_MAM_DEBUG, "queued duplicate %s "
			                  "dropped\n", d->key);
			x2_msg_free(d);
			continue;
		case X2_MAM_QUEUED_SKIPPED:
			purple_debug_info(X2_MAM_DEBUG, "queued offline copy %s "
			                  "in the history the page cap skipped\n",
			                  d->key);
			x2_msg_free(d);
			continue;
		case X2_MAM_QUEUED_DELIVER:
			break;
		}
		if (notice != NULL && !a->room && d->peer != NULL &&
		    g_hash_table_add(a->gap_noticed, g_strdup(d->peer)))
			x2_mam_notice(a, d->peer, notice);
		x2_mam_remember(a, d);
		x2_mam_advance_live(a, d);
		x2_msg_redeliver(a->st->js, d);
	}
	g_hash_table_remove_all(a->gap_noticed);
	x2_statefile_touch(a->st->sf);
}

void
x2_mam_sync_reset(X2MamArchive *a)
{
	if (a->watchdog != 0)
		purple_timeout_remove(a->watchdog);
	a->watchdog = 0;
	g_queue_clear_full(&a->queue, (GDestroyNotify)x2_msg_free);
	g_queue_clear_full(&a->page, (GDestroyNotify)x2_msg_free);
	g_free(a->queryid);
	a->queryid = NULL;
	g_free(a->iq_id);
	a->iq_id = NULL;
	g_free(a->skip_id);
	a->skip_id = NULL;
	a->phase = X2_MAM_IDLE;
}
