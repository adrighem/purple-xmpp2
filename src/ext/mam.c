/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file mam.c XEP-0313 account archive: hooks, result checks and dedup
 *
 * Inbound (ARCHITECTURE 6, steps 2, 3 and 6): archive results are verified
 * (core/mam_proto.c) and unwrapped; every 1:1 message is dropped when its
 * key was seen; live and carbon messages are held back while the catch-up
 * runs.  The catch-up itself is mam_sync.c.
 */
#include "xmpp2_internal.h"

#include "accountopt.h"
#include "conversation.h"
#include "debug.h"
#include "signals.h"

#include "core/dedup.h"
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

static X2Store *
store(X2MamState *st)
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
	char *key = x2_dedup_make_key(NULL, m->dir == X2_DIR_OUT, m->peer,
	                              origin);

	g_free(origin);
	return key;
}

gboolean
x2_mam_key_seen(X2MamState *st, const char *key)
{
	return key != NULL &&
	       (x2_dedup_contains(st->js->x2->dedup, key) ||
	        x2_store_ring_contains(store(st), st->own, key));
}

void
x2_mam_remember(X2MamState *st, const X2Msg *m)
{
	if (m->key == NULL)
		return;
	x2_dedup_add(st->js->x2->dedup, m->key);
	/* Keys without a trusted stanza-id are the ones the cursor cannot
	 * cover: keep them across restarts. */
	if (m->stanza_id == NULL) {
		x2_store_ring_add(store(st), st->own, m->key);
		x2_statefile_touch(st->sf);
	}
}

void
x2_mam_advance_live(X2MamState *st, const X2Msg *m)
{
	if (st->frozen || st->phase != X2_MAM_LIVE || m->stanza_id == NULL ||
	    !purple_strequal(m->archive, st->own))
		return;
	x2_store_set_cursor(store(st), st->own, m->stanza_id, m->ts);
	x2_statefile_touch(st->sf);
}

void
x2_mam_notice(X2MamState *st, const char *peer, const char *text)
{
	PurpleAccount *account = purple_connection_get_account(st->js->gc);
	PurpleConversation *conv;

	conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, peer,
	                                             account);
	if (conv == NULL)
		conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, peer);
	purple_conversation_write(conv, NULL, text,
	                          PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_NO_LOG,
	                          time(NULL));
}

/* ---- inbound ---- */

static X2Verdict
inbound_result(X2MamState *st, X2Msg *m, xmlnode *inner, const char *id)
{
	const char *type = xmlnode_get_attrib(inner, "type");
	X2Dir dir;
	char *ikey;
	gboolean dup;

	x2_mam_sync_kick(st);

	if (purple_strequal(type, "groupchat") || purple_strequal(type, "error")) {
		purple_debug_info(X2_MAM_DEBUG, "skipping archived %s message %s\n",
		                  type, id);
		return X2_CONSUMED;
	}

	dir = x2_mam_bare_equal(xmlnode_get_attrib(inner, "from"), st->own)
	      ? X2_DIR_OUT : X2_DIR_IN;
	x2_msg_set_inner(st->js, m, inner, X2_SRC_MAM, dir, st->own);
	/* The archive id is the stanza-id: the result comes from the archive
	 * we queried, checked above. */
	g_free(m->stanza_id);
	m->stanza_id = g_strdup(id);
	g_free(m->key);
	m->key = x2_dedup_make_key(id, dir == X2_DIR_OUT, m->peer, NULL);
	m->delayed = TRUE;
	st->page_ts = m->ts;

	if (st->kind == X2_MAMQ_CURSOR_ONLY || st->kind == X2_MAMQ_PROBE)
		return X2_CONSUMED;
	if (purple_strequal(id, st->skip_id)) {
		purple_debug_info(X2_MAM_DEBUG, "skipping old cursor %s\n", id);
		return X2_CONSUMED;
	}

	ikey = inner_key(m);
	dup = x2_mam_key_seen(st, m->key) || x2_mam_key_seen(st, ikey);
	g_free(ikey);
	if (dup) {
		purple_debug_info(X2_MAM_DEBUG, "duplicate archive result %s\n", id);
		x2_dedup_add(st->js->x2->dedup, m->key);
		return X2_CONSUMED;
	}

	if (st->kind == X2_MAMQ_LAST && m->peer != NULL &&
	    g_hash_table_add(st->gap_noticed, g_strdup(m->peer)))
		x2_mam_notice(st, m->peer, _("Older history skipped."));

	x2_mam_remember(st, m);
	return X2_PASS;
}

static X2Verdict
mam_inbound(JabberStream *js, X2Msg *m)
{
	X2MamState *st = x2_mam_state(js);
	xmlnode *inner;
	const char *id, *why;
	X2Msg *d;

	if (st == NULL)
		return X2_PASS;

	switch (x2_mam_result_check(m->outer, st->own, st->queryid, &inner,
	                            &id, &why)) {
	case X2_MAM_VALID:
		return inbound_result(st, m, inner, id);
	case X2_MAM_FORGED:
		purple_debug_warning(X2_MAM_DEBUG,
		                     "forged MAM result from %s dropped: %s\n",
		                     xmlnode_get_attrib(m->outer, "from")
		                     ? xmlnode_get_attrib(m->outer, "from") : "(none)",
		                     why);
		return X2_CONSUMED;
	case X2_MAM_NOT_RESULT:
		break;
	}

	/* Rooms are M4: only the account archive here */
	if (!purple_strequal(m->archive, st->own))
		return X2_PASS;

	if (x2_mam_key_seen(st, m->key)) {
		purple_debug_info(X2_MAM_DEBUG, "duplicate message %s dropped\n",
		                  m->key);
		return X2_CONSUMED;
	}

	if (st->phase == X2_MAM_SYNCING) {
		d = x2_msg_dup(m);
		if (d == NULL)
			return X2_PASS;
		g_queue_push_tail(&st->queue, d);
		purple_debug_info(X2_MAM_DEBUG, "queued during catch-up (%u)\n",
		                  g_queue_get_length(&st->queue));
		return X2_DEFERRED;
	}

	x2_mam_remember(st, m);
	x2_mam_advance_live(st, m);
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

	peer = jabber_get_bare_jid(to);
	key = x2_dedup_make_key(NULL, TRUE, peer, o->id);
	if (key != NULL) {
		x2_dedup_add(js->x2->dedup, key);
		x2_store_ring_add(store(st), st->own, key);
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

static void
mam_plugin_init(PurplePlugin *plugin)
{
	purple_signal_connect(purple_accounts_get_handle(), "account-removed",
	                      (void *)&x2_ext_mam,
	                      PURPLE_CALLBACK(account_removed_cb), NULL);
}

static void
mam_plugin_uninit(PurplePlugin *plugin)
{
	purple_signals_disconnect_by_handle((void *)&x2_ext_mam);
}

static void
mam_account_options(GList **opts)
{
	*opts = g_list_append(*opts, purple_account_option_int_new(
		_("Days of history to fetch on first login (0: none)"),
		"mam_initial_days", 1));
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

	st->js = js;
	st->own = jabber_id_get_bare_jid(js->user);
	st->sf = x2_statefile_open(purple_connection_get_account(js->gc));
	st->gap_noticed = g_hash_table_new_full(g_str_hash, g_str_equal,
	                                        g_free, NULL);
	g_queue_init(&st->queue);
	x2_store_ring_foreach(store(st), seed_dedup, js->x2->dedup);
	x2_ext_set_priv(js, &x2_ext_mam, st);
}

static void
mam_stream_closing(JabberStream *js)
{
	X2MamState *st = x2_mam_state(js);

	if (st == NULL)
		return;
	if (st->phase == X2_MAM_SYNCING)
		x2_mam_sync_finish(st, FALSE, NULL);
	x2_statefile_flush(st->sf);
}

static void
mam_stream_free(JabberStream *js)
{
	X2MamState *st = x2_mam_state(js);

	if (st == NULL)
		return;
	x2_mam_sync_free(st);
	x2_statefile_close(st->sf);
	g_hash_table_destroy(st->gap_noticed);
	g_free(st->own);
	g_free(st);
	x2_ext_set_priv(js, &x2_ext_mam, NULL);
}

static gboolean
has_feature(xmlnode *query, const char *var)
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
	if (is_own && has_feature(query, NS_MAM))
		st->supported = TRUE;
	if (is_own && has_feature(query, X2_NS_MAM_EXTENDED))
		st->extended = TRUE;
	if ((is_own || purple_strequal(from, js->user->domain)) &&
	    has_feature(query, NS_CARBONS))
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
		x2_mam_sync_begin(st);
	else {
		purple_debug_info(X2_MAM_DEBUG, "account archive: no MAM support\n");
		st->phase = X2_MAM_LIVE;
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
	.stream_closing = mam_stream_closing,
	.carbons_settled = mam_carbons_settled,
};
