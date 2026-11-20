/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file mam.c XEP-0313 archives: hooks, result checks and dedup
 *
 * Inbound (ARCHITECTURE 6, steps 2, 3 and 6): archive results are verified
 * (core/mam_proto.c) and unwrapped; every message of an archive we follow
 * (the account, joined MAM rooms) is dropped when its key was seen; live
 * and carbon messages are held back while that archive's catch-up runs.
 * The catch-up itself is mam_sync.c, the rooms mam_muc.c.
 */
#include "xmpp2_internal.h"

#include "accountopt.h"
#include "conversation.h"
#include "debug.h"
#include "eventloop.h"
#include "signals.h"

#include "core/dedup.h"
#include "core/jid.h"
#include "core/mam_proto.h"
#include "core/sid.h"
#include "ext/mam.h"
#include "jutil.h"
#include "namespaces.h"

X2MamState *
x2_mam_state(JabberStream *js)
{
	return js->x2 ? x2_ext_get_priv(js, &x2_ext_mam) : NULL;
}

X2Store *
x2_mam_store(X2MamState *st)
{
	return x2_statefile_store(st->sf);
}

/* ---- dedup and cursor ---- */

/* The origin-id key of a message, consulted for archive results in addition
 * to their stanza-id key (ARCHITECTURE 7.3). */
static char *
inner_key(const X2Msg *m)
{
	char *origin = x2_sid_origin_id(m->msg);
	char *key = x2_dedup_make_key(NULL, NULL, m->dir == X2_DIR_OUT, m->peer,
	                              origin);

	g_free(origin);
	return key;
}

gboolean
x2_mam_key_seen(X2MamArchive *a, const char *key)
{
	return key != NULL &&
	       (x2_dedup_contains(a->st->js->x2->dedup, key) ||
	        x2_store_ring_contains(x2_mam_store(a->st), a->jid, key));
}

void
x2_mam_remember(X2MamArchive *a, const X2Msg *m)
{
	if (m->key == NULL)
		return;
	x2_dedup_add(a->st->js->x2->dedup, m->key);
	/* Keys without a trusted stanza-id are the ones the cursor cannot
	 * cover: keep them across restarts.  So are all keys while the cursor
	 * is frozen: the next login fetches that gap again. */
	if (m->stanza_id == NULL || a->frozen) {
		x2_store_ring_add(x2_mam_store(a->st), a->jid, m->key);
		x2_statefile_touch(a->st->sf);
	}
}

void
x2_mam_advance_live(X2MamArchive *a, const X2Msg *m)
{
	if (a->frozen || a->phase != X2_MAM_LIVE || m->stanza_id == NULL ||
	    !purple_strequal(m->archive, a->jid))
		return;
	x2_store_set_cursor(x2_mam_store(a->st), a->jid, m->stanza_id, m->ts);
	x2_statefile_touch(a->st->sf);
}

void
x2_mam_notice(X2MamArchive *a, const char *peer, const char *text)
{
	PurpleAccount *account = purple_connection_get_account(a->st->js->gc);
	PurpleConversation *conv;

	if (a->room) {
		/* Only into the open room: a left room gets no line */
		conv = purple_find_conversation_with_account(
			PURPLE_CONV_TYPE_CHAT, a->jid, account);
		if (conv != NULL)
			purple_conv_chat_write(PURPLE_CONV_CHAT(conv), "", text,
			                       PURPLE_MESSAGE_SYSTEM |
			                       PURPLE_MESSAGE_NO_LOG, time(NULL));
		return;
	}
	conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, peer,
	                                             account);
	if (conv == NULL)
		conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, peer);
	purple_conversation_write(conv, NULL, text,
	                          PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_NO_LOG,
	                          time(NULL));
}

/* ---- inbound ---- */

/*
 * TRUE if a live (undelayed) copy of an archive result with key @a key or
 * @a ikey is held in the catch-up queue.  A message archived after the
 * catch-up began but before the server ran the query comes both live and
 * as a result; the live copy is the one to show, as it came (not delayed).
 */
static gboolean
queued_live(X2MamArchive *a, const char *key, const char *ikey)
{
	GList *l;

	for (l = a->queue.head; l != NULL; l = l->next) {
		const X2Msg *d = l->data;

		if (d->delayed || d->key == NULL)
			continue;
		if (purple_strequal(d->key, key) || purple_strequal(d->key, ikey))
			return TRUE;
	}
	return FALSE;
}

X2Verdict
x2_mam_inbound_result(X2MamArchive *a, X2Msg *m, xmlnode *inner,
                      const char *id)
{
	const char *type = xmlnode_get_attrib(inner, "type");
	X2Dir dir;
	X2Msg *d;
	char *ikey;
	gboolean dup;

	/* The newest page is buffered until its <fin/>: an archive that sends
	 * more than the max we asked for is forged or broken, and must neither
	 * grow the buffer nor keep the watchdog from firing. */
	if (a->kind == X2_MAMQ_LAST) {
		if (a->page_results >= x2_mam_page_limit(a, a->kind)) {
			purple_debug_warning(X2_MAM_DEBUG, "archive result %s dropped: "
			                     "more results than the page size %u "
			                     "(forged?)\n", id,
			                     x2_mam_page_limit(a, a->kind));
			return X2_CONSUMED;
		}
		a->page_results++;
	}
	x2_mam_sync_kick(a);

	/* (a room result was forced to groupchat by its check) */
	if (!a->room && (purple_strequal(type, "groupchat") ||
	                 purple_strequal(type, "error"))) {
		purple_debug_info(X2_MAM_DEBUG, "skipping archived %s message %s\n",
		                  type, id);
		return X2_CONSUMED;
	}

	/* In a room our own messages come back from our nick, and libpurple
	 * marks them as sent by the nick (serv_got_chat_in()), as live. */
	dir = !a->room &&
	      x2_jid_bare_equal(xmlnode_get_attrib(inner, "from"), a->st->own)
	      ? X2_DIR_OUT : X2_DIR_IN;
	x2_msg_set_inner(a->st->js, m, inner, X2_SRC_MAM, dir, a->jid);
	/* The archive id is the stanza-id: the result comes from the archive
	 * we queried, checked above. */
	g_free(m->stanza_id);
	m->stanza_id = g_strdup(id);
	g_free(m->key);
	m->key = x2_dedup_make_key(id, a->jid, dir == X2_DIR_OUT, m->peer, NULL);
	m->delayed = TRUE;
	a->page_ts = m->ts;

	if (a->kind == X2_MAMQ_CURSOR_ONLY || a->kind == X2_MAMQ_PROBE)
		return X2_CONSUMED;
	if (purple_strequal(id, a->skip_id)) {
		purple_debug_info(X2_MAM_DEBUG, "skipping old cursor %s\n", id);
		return X2_CONSUMED;
	}

	ikey = inner_key(m);
	dup = x2_mam_key_seen(a, m->key) || x2_mam_key_seen(a, ikey);
	if (!dup && queued_live(a, m->key, ikey)) {
		g_free(ikey);
		purple_debug_info(X2_MAM_DEBUG, "archive result %s is queued "
		                  "live, shown live\n", id);
		return X2_CONSUMED;
	}
	g_free(ikey);
	if (dup) {
		purple_debug_info(X2_MAM_DEBUG, "duplicate archive result %s\n", id);
		x2_dedup_add(a->st->js->x2->dedup, m->key);
		return X2_CONSUMED;
	}

	/* The newest page: whether older history was skipped is known only
	 * at its <fin/> (mam_sync.c flush_page()) */
	if (a->kind == X2_MAMQ_LAST) {
		d = x2_msg_dup(m);
		if (d == NULL)
			return X2_PASS;
		g_queue_push_tail(&a->page, d);
		return X2_DEFERRED;
	}

	x2_mam_remember(a, m);
	return X2_PASS;
}

/* A <result/>: from the account archive (no from, or our bare JID) or from
 * a room we are catching up; anything else is forged. */
static X2Verdict
inbound_result(X2MamState *st, X2Msg *m)
{
	const char *from = xmlnode_get_attrib(m->outer, "from");
	X2MamArchive *a = NULL;
	xmlnode *inner;
	const char *id, *why;
	X2MamCheck c;

	if (from == NULL || purple_strequal(from, st->own)) {
		a = st->acct;
		c = x2_mam_result_check(m->outer, st->own, a->queryid, &inner,
		                        &id, &why);
	} else if ((a = x2_mam_muc_archive(st, from)) != NULL) {
		c = x2_mam_room_result_check(m->outer, a->jid, a->queryid,
		                             &inner, &id, &why);
	} else {
		c = x2_mam_result_check(m->outer, st->own, NULL, &inner, &id,
		                        &why);
	}

	switch (c) {
	case X2_MAM_VALID:
		return x2_mam_inbound_result(a, m, inner, id);
	case X2_MAM_FORGED:
		purple_debug_warning(X2_MAM_DEBUG,
		                     "forged MAM result from %s dropped: %s\n",
		                     from ? from : "(none)", why);
		return X2_CONSUMED;
	case X2_MAM_NOT_RESULT:
		break;
	}
	return X2_PASS;
}

static X2Verdict
mam_inbound(JabberStream *js, X2Msg *m)
{
	X2MamState *st = x2_mam_state(js);
	X2MamArchive *a;
	X2Msg *d;

	if (st == NULL)
		return X2_PASS;

	if (xmlnode_get_child_with_namespace(m->outer, "result", NS_MAM))
		return inbound_result(st, m);

	/* The account archive or a joined MAM room; other rooms unchanged */
	if (purple_strequal(m->archive, st->own))
		a = st->acct;
	else if ((a = x2_mam_muc_archive(st, m->archive)) == NULL)
		return X2_PASS;

	if (x2_mam_key_seen(a, m->key)) {
		purple_debug_info(X2_MAM_DEBUG, "duplicate message %s dropped\n",
		                  m->key);
		return X2_CONSUMED;
	}

	/* Bodyless messages (chat states, receipts, markers, a room subject)
	 * are not history: holding them back only delays them. */
	if (a->phase == X2_MAM_SYNCING &&
	    xmlnode_get_child(m->msg, "body") == NULL)
		return X2_PASS;

	if (a->phase == X2_MAM_SYNCING &&
	    g_queue_get_length(&a->queue) >= X2_MAM_QUEUE_MAX) {
		purple_debug_warning(X2_MAM_DEBUG, "catch-up queue full (%u)\n",
		                     X2_MAM_QUEUE_MAX);
		x2_mam_sync_finish(a, FALSE,
		                   _("Message history may be incomplete: too many "
		                     "messages arrived while it was fetched."));
	}

	if (a->phase == X2_MAM_SYNCING) {
		d = x2_msg_dup(m);
		if (d == NULL)
			return X2_PASS;
		g_queue_push_tail(&a->queue, d);
		purple_debug_info(X2_MAM_DEBUG, "queued during catch-up (%u)\n",
		                  g_queue_get_length(&a->queue));
		return X2_DEFERRED;
	}

	x2_mam_remember(a, m);
	x2_mam_advance_live(a, m);
	return X2_PASS;
}

/* Our own sends: a later catch-up must not show them twice. */
static void
mam_outbound(JabberStream *js, xmlnode *message, X2Out *o)
{
	X2MamState *st = x2_mam_state(js);
	const char *to = xmlnode_get_attrib(message, "to");
	char *peer, *key;

	if (st == NULL || o->groupchat || !o->has_body || o->id == NULL ||
	    to == NULL)
		return;

	/* The same conversation key as the archived copy gets on the way in
	 * (full JID for a private message to a room occupant) */
	peer = x2_msg_peer_of(js, to, xmlnode_get_attrib(message, "type"),
	                      message);
	key = x2_dedup_make_key(NULL, NULL, TRUE, peer, o->id);
	if (key != NULL) {
		x2_dedup_add(js->x2->dedup, key);
		x2_store_ring_add(x2_mam_store(st), st->own, key);
		x2_statefile_touch(st->sf);
	}
	g_free(key);
	g_free(peer);
}

/* ---- lifecycle hooks ---- */

static void
account_removed_cb(PurpleAccount *account, gpointer data)
{
	if (purple_strequal(purple_account_get_protocol_id(account),
	                    XMPP2_PRPL_ID))
		x2_statefile_remove(account);
}

/*
 * Prpls are loaded before purple_accounts_init() registers "account-removed"
 * (see stream_management.c): connect once the main loop runs, which is
 * after the core is initialised, or on the first stream if that is sooner.
 */
static gboolean removed_connected;
static guint removed_timer;

static void
connect_account_removed(void)
{
	if (removed_connected)
		return;
	if (purple_signal_connect(purple_accounts_get_handle(),
	                          "account-removed", (void *)&x2_ext_mam,
	                          PURPLE_CALLBACK(account_removed_cb),
	                          NULL) == 0) {
		purple_debug_error(X2_MAM_DEBUG,
		                   "cannot connect to account-removed\n");
		return;
	}
	removed_connected = TRUE;
}

static gboolean
connect_account_removed_cb(gpointer data)
{
	removed_timer = 0;
	connect_account_removed();
	return FALSE;
}

static void
mam_plugin_init(PurplePlugin *plugin)
{
	removed_timer = purple_timeout_add(0, connect_account_removed_cb, NULL);
}

static void
mam_plugin_uninit(PurplePlugin *plugin)
{
	if (removed_timer != 0)
		purple_timeout_remove(removed_timer);
	removed_timer = 0;
	if (removed_connected)
		purple_signals_disconnect_by_handle((void *)&x2_ext_mam);
	removed_connected = FALSE;
}

static void
mam_account_options(GList **opts)
{
	*opts = g_list_append(*opts, purple_account_option_int_new(
		_("Days of history to fetch on first login (0: none)"),
		"mam_initial_days", 1));
	x2_mam_muc_account_options(opts);
}

static void
seed_dedup(gpointer key, gpointer data)
{
	x2_dedup_add(data, key);
}

static void
mam_stream_new(JabberStream *js)
{
	X2MamState *st = g_new0(X2MamState, 1);

	connect_account_removed();
	st->js = js;
	st->own = jabber_id_get_bare_jid(js->user);
	st->sf = x2_statefile_open(purple_connection_get_account(js->gc));
	st->acct = x2_mam_archive_new(st, st->own, FALSE);
	x2_mam_muc_stream_new(st);
	x2_store_ring_foreach(x2_mam_store(st), seed_dedup, js->x2->dedup);
	x2_ext_set_priv(js, &x2_ext_mam, st);
}

static void
mam_stream_closing(JabberStream *js)
{
	X2MamState *st = x2_mam_state(js);
	GHashTableIter it;
	gpointer room;

	if (st == NULL)
		return;
	if (st->acct->phase == X2_MAM_SYNCING)
		x2_mam_sync_finish(st->acct, FALSE, NULL);
	g_hash_table_iter_init(&it, st->rooms);
	while (g_hash_table_iter_next(&it, NULL, &room))
		if (((X2MamArchive *)room)->phase == X2_MAM_SYNCING)
			x2_mam_sync_finish(room, FALSE, NULL);
	x2_statefile_flush(st->sf);
}

static void
mam_stream_free(JabberStream *js)
{
	X2MamState *st = x2_mam_state(js);

	if (st == NULL)
		return;
	x2_mam_muc_stream_free(st);
	x2_mam_archive_free(st->acct);
	x2_statefile_close(st->sf);
	g_free(st->own);
	g_free(st);
	x2_ext_set_priv(js, &x2_ext_mam, NULL);
}

gboolean
x2_mam_has_feature(xmlnode *query, const char *var)
{
	xmlnode *f;

	for (f = xmlnode_get_child(query, "feature"); f;
	     f = xmlnode_get_next_twin(f))
		if (purple_strequal(xmlnode_get_attrib(f, "var"), var))
			return TRUE;
	return FALSE;
}

static void
mam_server_info(JabberStream *js, const char *from, xmlnode *query)
{
	X2MamState *st = x2_mam_state(js);
	char *own = jabber_id_get_bare_jid(js->user);
	gboolean is_own = purple_strequal(from, own);

	if (st == NULL) {
		g_free(own);
		return;
	}
	if (is_own && x2_mam_has_feature(query, NS_MAM))
		st->supported = TRUE;
	if (is_own && x2_mam_has_feature(query, X2_NS_MAM_EXTENDED))
		st->acct->extended = TRUE;
	if ((is_own || purple_strequal(from, js->user->domain)) &&
	    x2_mam_has_feature(query, NS_CARBONS))
		st->carbons_offered = TRUE;
	g_free(own);
}

static void
mam_pre_presence(JabberStream *js)
{
	X2MamState *st = x2_mam_state(js);

	if (st == NULL)
		return;
	if (st->supported)
		x2_mam_sync_begin(st->acct);
	else {
		purple_debug_info(X2_MAM_DEBUG, "account archive: no MAM support\n");
		st->acct->phase = X2_MAM_LIVE;
	}
}

static void
mam_session_ready(JabberStream *js)
{
	X2MamState *st = x2_mam_state(js);

	if (st == NULL)
		return;
	st->ready = TRUE;
	x2_mam_sync_maybe_start(st);
}

static void
mam_carbons_settled(JabberStream *js)
{
	X2MamState *st = x2_mam_state(js);

	if (st == NULL)
		return;
	st->carbons_settled = TRUE;
	x2_mam_sync_maybe_start(st);
}

const X2Ext x2_ext_mam = {
	.name = "mam",
	.plugin_init = mam_plugin_init,
	.plugin_uninit = mam_plugin_uninit,
	.account_options = mam_account_options,
	.stream_new = mam_stream_new,
	.stream_free = mam_stream_free,
	.server_info = mam_server_info,
	.pre_presence = mam_pre_presence,
	.session_ready = mam_session_ready,
	.inbound = mam_inbound,
	.outbound = mam_outbound,
	.chat_joining = x2_mam_muc_chat_joining,
	.chat_join_presence = x2_mam_muc_join_presence,
	.chat_joined = x2_mam_muc_chat_joined,
	.stream_closing = mam_stream_closing,
	.carbons_settled = mam_carbons_settled,
};
