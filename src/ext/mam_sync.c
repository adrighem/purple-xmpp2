/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file mam_sync.c XEP-0313 account archive catch-up (ARCHITECTURE 7.2)
 *
 *   IDLE --pre_presence--> SYNCING --fin complete--> LIVE
 *                            |  ^ fin incomplete: next page (after=<last>)
 *                            |  item-not-found on after=cursor: retry once
 *                            |  with start=cursor_ts
 *                            +--error, watchdog, teardown--> LIVE
 *
 * The first query waits for session_ready and, when the server offers
 * carbons and the carbons module is registered, for x2_ext_carbons_settled().
 */
#include "xmpp2_internal.h"

#include <stdlib.h>

#include "debug.h"
#include "eventloop.h"

#include "core/mam_proto.h"
#include "core/sid.h"
#include "ext/mam.h"
#include "iq.h"

/* Test hook: build and log the query but never send it, so the watchdog
 * fires (tests/integration/test_mam_recovery.sh). */
#define X2_MAM_STALL_ENV "XMPP2_TEST_MAM_STALL"

static void send_query(X2MamState *st, X2MamQueryKind kind,
                       const char *after, time_t start, const char *id);

static X2Store *
store(X2MamState *st)
{
	return x2_statefile_store(st->sf);
}

static gboolean
watchdog_cb(gpointer data)
{
	X2MamState *st = data;

	st->watchdog = 0;
	if (st->phase != X2_MAM_SYNCING)
		return FALSE;
	if (st->queryid == NULL && st->ready) {
		purple_debug_warning(X2_MAM_DEBUG, "carbons enable unanswered, "
		                     "querying the archive anyway\n");
		st->carbons_settled = TRUE;
		x2_mam_sync_maybe_start(st);
		return FALSE;
	}
	purple_debug_warning(X2_MAM_DEBUG, "watchdog: no archive answer in "
	                     "%d s\n", X2_MAM_WATCHDOG_SECS);
	x2_mam_sync_finish(st, FALSE,
	                   _("Message history may be incomplete: the server "
	                     "did not answer the history request in time."));
	return FALSE;
}

void
x2_mam_sync_kick(X2MamState *st)
{
	if (st->watchdog != 0)
		purple_timeout_remove(st->watchdog);
	st->watchdog = 0;
	if (st->phase == X2_MAM_SYNCING)
		st->watchdog = purple_timeout_add_seconds(X2_MAM_WATCHDOG_SECS,
		                                          watchdog_cb, st);
}

void
x2_mam_sync_begin(X2MamState *st)
{
	purple_debug_info(X2_MAM_DEBUG, "account archive SYNCING\n");
	st->phase = X2_MAM_SYNCING;
	x2_mam_sync_kick(st);
}

/* No cursor yet: the first-run rule (ARCHITECTURE 7.2) */
static void
first_run(X2MamState *st)
{
	PurpleAccount *account = purple_connection_get_account(st->js->gc);
	int days = purple_account_get_int(account, "mam_initial_days", 1);

	if (x2_store_cursor_only(store(st)) || days <= 0)
		send_query(st, X2_MAMQ_CURSOR_ONLY, NULL, 0, NULL);
	else
		send_query(st, X2_MAMQ_FORWARD, NULL,
		           time(NULL) - (time_t)days * 24 * 3600, NULL);
}

void
x2_mam_sync_maybe_start(X2MamState *st)
{
	const char *cursor;

	if (st->phase != X2_MAM_SYNCING || !st->ready || st->queryid != NULL)
		return;
	if (st->carbons_offered && !st->carbons_settled &&
	    x2_ext_registered("carbons")) {
		purple_debug_info(X2_MAM_DEBUG, "waiting for carbons enable\n");
		return;
	}

	cursor = x2_store_get_cursor(store(st), st->own, NULL);
	st->from_cursor = cursor != NULL;
	if (cursor != NULL)
		send_query(st, X2_MAMQ_FORWARD, cursor, 0, NULL);
	else
		first_run(st);
}

/* item-not-found on after=cursor: the cursor left the archive (or is
 * corrupt).  Retry once from its timestamp, else apply the first-run rule. */
static void
retry_from_ts(X2MamState *st)
{
	gint64 ts = 0;
	const char *cursor = x2_store_get_cursor(store(st), st->own, &ts);

	st->retried = TRUE;
	st->from_cursor = FALSE;
	st->pages = 0;
	if (cursor != NULL && ts > 0) {
		purple_debug_info(X2_MAM_DEBUG, "cursor %s unknown to the "
		                  "archive, retrying from %" G_GINT64_FORMAT "\n",
		                  cursor, ts);
		g_free(st->skip_id);
		st->skip_id = g_strdup(cursor);
		send_query(st, X2_MAMQ_FORWARD, NULL, (time_t)ts, NULL);
	} else {
		purple_debug_info(X2_MAM_DEBUG, "cursor unknown to the archive, "
		                  "no timestamp: first-run rule\n");
		first_run(st);
	}
}

static void
query_cb(JabberStream *js, const char *from, JabberIqType type,
         const char *id, xmlnode *packet, gpointer data)
{
	X2MamState *st = x2_mam_state(js);
	X2MamFin fin;

	/* The stream may have moved on (watchdog, newer query): ignore */
	if (st == NULL || st->phase != X2_MAM_SYNCING ||
	    !purple_strequal(id, st->iq_id))
		return;

	if (type == JABBER_IQ_ERROR) {
		if (x2_mam_error_is_item_not_found(packet) && !st->retried &&
		    st->pages == 0 && st->kind == X2_MAMQ_FORWARD &&
		    x2_store_get_cursor(store(st), st->own, NULL) != NULL) {
			retry_from_ts(st);
			return;
		}
		purple_debug_error(X2_MAM_DEBUG, "archive query failed\n");
		x2_mam_sync_finish(st, FALSE,
		                   _("Message history may be incomplete: the "
		                     "server refused the history request."));
		return;
	}

	if (!x2_mam_fin_parse(packet, &fin)) {
		purple_debug_error(X2_MAM_DEBUG, "archive answer without <fin/>\n");
		x2_mam_sync_finish(st, FALSE, NULL);
		return;
	}

	st->pages++;
	purple_debug_info(X2_MAM_DEBUG, "MAM page %u: complete=%s last=%s\n",
	                  st->pages, fin.complete ? "true" : "false",
	                  fin.last ? fin.last : "(none)");

	if (st->kind == X2_MAMQ_PROBE) {
		st->covered = fin.last != NULL;
		if (fin.last != NULL)
			x2_mam_sync_finish(st, TRUE, NULL);
		else
			retry_from_ts(st);
		x2_mam_fin_clear(&fin);
		return;
	}

	/* Some archives (Prosody's internal storage) answer an unknown
	 * after=<id> with an empty page instead of item-not-found: make sure
	 * an empty answer means "nothing new" and not "cursor gone". */
	if (st->from_cursor && st->pages == 1 && fin.last == NULL &&
	    st->extended && !st->retried) {
		send_query(st, X2_MAMQ_PROBE, NULL, 0,
		           x2_store_get_cursor(store(st), st->own, NULL));
		x2_mam_fin_clear(&fin);
		return;
	}

	if (fin.last != NULL || !st->from_cursor)
		st->covered = TRUE;

	if (fin.last != NULL) {
		x2_store_set_cursor(store(st), st->own, fin.last,
		                    st->page_ts ? (gint64)st->page_ts
		                                : (gint64)time(NULL));
		x2_statefile_touch(st->sf);
	}

	if (fin.complete || fin.last == NULL || st->kind != X2_MAMQ_FORWARD)
		x2_mam_sync_finish(st, TRUE, NULL);
	else if (st->pages >= X2_MAM_MAX_PAGES - 1) {
		purple_debug_info(X2_MAM_DEBUG, "page cap: jumping to the "
		                  "newest page\n");
		send_query(st, X2_MAMQ_LAST, NULL, 0, NULL);
	} else
		send_query(st, X2_MAMQ_FORWARD, fin.last, 0, NULL);

	x2_mam_fin_clear(&fin);
}

static void
send_query(X2MamState *st, X2MamQueryKind kind, const char *after,
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
	q.max = kind == X2_MAMQ_CURSOR_ONLY || kind == X2_MAMQ_PROBE
	        ? 1 : X2_MAM_PAGE_SIZE;
	query = x2_mam_query_new(&q);
	if (query == NULL) {
		g_free(queryid);
		x2_mam_sync_finish(st, FALSE, NULL);
		return;
	}

	iq = jabber_iq_new(st->js, JABBER_IQ_SET);
	xmlnode_insert_child(iq->node, query);
	jabber_iq_set_callback(iq, query_cb, NULL);

	g_free(st->queryid);
	st->queryid = queryid;
	g_free(st->iq_id);
	st->iq_id = g_strdup(iq->id);
	st->kind = kind;
	st->page_ts = 0;
	x2_mam_sync_kick(st);

	purple_debug_info(X2_MAM_DEBUG, "MAM query %s: %s after=%s start=%ld\n",
	                  st->iq_id,
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
x2_mam_sync_finish(X2MamState *st, gboolean complete, const char *notice)
{
	GHashTable *told;
	X2Msg *d;

	if (st->watchdog != 0)
		purple_timeout_remove(st->watchdog);
	st->watchdog = 0;
	g_free(st->queryid);
	st->queryid = NULL;
	g_free(st->iq_id);
	st->iq_id = NULL;
	g_free(st->skip_id);
	st->skip_id = NULL;
	st->phase = X2_MAM_LIVE;
	if (!complete)
		st->frozen = TRUE;

	purple_debug_info(X2_MAM_DEBUG, "account archive LIVE (%s), %u queued\n",
	                  complete ? "complete" : "incomplete",
	                  g_queue_get_length(&st->queue));

	/* Delivered in arrival order; dedup again, the catch-up may have
	 * shown some of them already. */
	told = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	while ((d = g_queue_pop_head(&st->queue)) != NULL) {
		if (x2_mam_key_seen(st, d->key)) {
			purple_debug_info(X2_MAM_DEBUG, "queued duplicate %s "
			                  "dropped\n", d->key);
			x2_msg_free(d);
			continue;
		}
		/* Offline storage (XEP-0160) replays what the archive already
		 * had when we queried it: a delayed message with an archive
		 * stanza-id lies inside the range a complete catch-up covered,
		 * or in the part the page cap skipped on purpose.  Not when an
		 * empty answer may hide a lost cursor (!covered).  A cursor-only
		 * sync showed nothing, so there the offline copy is the news. */
		if (complete && st->covered && st->kind != X2_MAMQ_CURSOR_ONLY &&
		    d->delayed && d->stanza_id != NULL) {
			purple_debug_info(X2_MAM_DEBUG, "queued offline copy %s "
			                  "covered by the archive\n", d->key);
			x2_msg_free(d);
			continue;
		}
		if (notice != NULL && d->peer != NULL &&
		    g_hash_table_add(told, g_strdup(d->peer)))
			x2_mam_notice(st, d->peer, notice);
		x2_mam_remember(st, d);
		x2_mam_advance_live(st, d);
		x2_msg_redeliver(st->js, d);
	}
	g_hash_table_destroy(told);
	x2_statefile_touch(st->sf);
}

void
x2_mam_sync_free(X2MamState *st)
{
	if (st->watchdog != 0)
		purple_timeout_remove(st->watchdog);
	st->watchdog = 0;
	g_queue_clear_full(&st->queue, (GDestroyNotify)x2_msg_free);
	g_free(st->queryid);
	st->queryid = NULL;
	g_free(st->iq_id);
	st->iq_id = NULL;
	g_free(st->skip_id);
	st->skip_id = NULL;
}
