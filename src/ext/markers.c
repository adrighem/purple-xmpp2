/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file markers.c XEP-0333 chat markers (ARCHITECTURE 7.5)
 *
 * Outgoing 1:1 messages are markable.  Incoming markable messages are
 * remembered per contact; when the conversation is read (Pidgin resets its
 * unseen count, signal conversation-updated) one "displayed" marker goes out
 * for the newest of them.  Incoming markers raise "xmpp2-marker".
 */
#include "xmpp2_internal.h"

#include "account.h"
#include "conversation.h"
#include "debug.h"
#include "signals.h"

#include "core/dedup.h"
#include "core/receipt_parse.h"
#include "ext/ext.h"
#include "ext/peerutil.h"
#include "jutil.h"
#include "namespaces.h"

#define LOG "xmpp2-markers"

/* How many sent messages we remember to match markers against */
#define SENT_CAPACITY 256

extern const X2Ext x2_ext_markers;

/* Per contact: the newest markable message and what we told them */
typedef struct {
	char *last_id;     /* newest markable message received */
	time_t last_ts;
	char *sent_id;     /* message we last sent a displayed marker for */
} PeerState;

typedef struct {
	X2Dedup *sent;     /* messages we made markable, see x2_ack_key() */
	GHashTable *peers; /* bare JID -> PeerState */
} MarkersPriv;

static PurplePlugin *signal_plugin;

static void
peer_state_free(gpointer data)
{
	PeerState *st = data;

	g_free(st->last_id);
	g_free(st->sent_id);
	g_free(st);
}

static MarkersPriv *
get_priv(JabberStream *js)
{
	return x2_ext_get_priv(js, &x2_ext_markers);
}

/* Pidgin sets "unseen-count" to 0 when the user looks at the conversation */
static void
conversation_updated_cb(PurpleConversation *conv, PurpleConvUpdateType type)
{
	PurpleAccount *account;
	PurpleConnection *gc;
	JabberStream *js;
	MarkersPriv *priv;
	PeerState *st;
	xmlnode *marker;
	char *bare;

	if (type != PURPLE_CONV_UPDATE_UNSEEN ||
	    purple_conversation_get_type(conv) != PURPLE_CONV_TYPE_IM ||
	    GPOINTER_TO_INT(purple_conversation_get_data(conv,
	                                                 "unseen-count")) != 0)
		return;

	account = purple_conversation_get_account(conv);
	if (!purple_strequal(purple_account_get_protocol_id(account),
	                     XMPP2_PRPL_ID))
		return;
	gc = purple_account_get_connection(account);
	if (gc == NULL || !PURPLE_CONNECTION_IS_CONNECTED(gc))
		return;
	js = purple_connection_get_protocol_data(gc);
	if (js == NULL || js->x2 == NULL)
		return;

	priv = get_priv(js);
	bare = jabber_get_bare_jid(purple_conversation_get_name(conv));
	st = bare ? g_hash_table_lookup(priv->peers, bare) : NULL;
	if (st == NULL || st->last_id == NULL) {
		g_free(bare);
		return;
	}
	if (purple_strequal(st->sent_id, st->last_id)) {
		purple_debug_info(LOG, "displayed for %s already sent to %s\n",
		                  st->last_id, bare);
		g_free(bare);
		return;
	}

	marker = x2_marker_new(bare, X2_MARKER_DISPLAYED, st->last_id);
	jabber_send(js, marker);
	xmlnode_free(marker);
	purple_debug_info(LOG, "sent displayed for %s to %s\n", st->last_id,
	                  bare);
	g_free(st->sent_id);
	st->sent_id = g_strdup(st->last_id);
	g_free(bare);
}

static void
markers_plugin_init(PurplePlugin *plugin)
{
	signal_plugin = plugin;
	jabber_add_feature(NS_CHAT_MARKERS, NULL);

	/* account, bare JID of the contact, id of the marked message, marker
	 * name: "received", "displayed" or "acknowledged" */
	purple_signal_register(plugin, "xmpp2-marker",
		purple_marshal_VOID__POINTER_POINTER_POINTER_POINTER, NULL, 4,
		purple_value_new(PURPLE_TYPE_SUBTYPE, PURPLE_SUBTYPE_ACCOUNT),
		purple_value_new(PURPLE_TYPE_STRING),
		purple_value_new(PURPLE_TYPE_STRING),
		purple_value_new(PURPLE_TYPE_STRING));

	purple_signal_connect(purple_conversations_get_handle(),
	                      "conversation-updated", plugin,
	                      PURPLE_CALLBACK(conversation_updated_cb), NULL);
}

static void
markers_plugin_uninit(PurplePlugin *plugin)
{
	purple_signals_disconnect_by_handle(plugin);
	jabber_remove_feature(NS_CHAT_MARKERS);
	signal_plugin = NULL;
}

static void
markers_stream_new(JabberStream *js)
{
	MarkersPriv *priv = g_new0(MarkersPriv, 1);

	priv->sent = x2_dedup_new(SENT_CAPACITY);
	priv->peers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
	                                    peer_state_free);
	x2_ext_set_priv(js, &x2_ext_markers, priv);
}

static void
markers_stream_free(JabberStream *js)
{
	MarkersPriv *priv = get_priv(js);

	if (priv == NULL)
		return;
	x2_dedup_free(priv->sent);
	g_hash_table_destroy(priv->peers);
	g_free(priv);
	x2_ext_set_priv(js, &x2_ext_markers, NULL);
}

static void
markers_outbound(JabberStream *js, xmlnode *message, X2Out *o)
{
	const char *to = xmlnode_get_attrib(message, "to");
	char *key;

	if (o->groupchat || !o->has_body || o->id == NULL || to == NULL)
		return;
	if (!x2_peer_wants(js, to, NS_CHAT_MARKERS))
		return;

	x2_marker_add_markable(message);
	key = x2_ack_key(to, o->id);
	x2_dedup_add(get_priv(js)->sent, key);
	g_free(key);
}

/* A contact marks a message of ours. */
static void
got_marker(JabberStream *js, const char *from, X2MarkerType type,
           const char *id)
{
	PurpleAccount *account = purple_connection_get_account(js->gc);
	char *key = x2_ack_key(from, id);
	char *bare;

	if (!x2_dedup_contains(get_priv(js)->sent, key)) {
		purple_debug_info(LOG, "ignoring %s marker %s from %s: we did "
		                  "not ask that contact\n", x2_marker_name(type),
		                  id, from);
		g_free(key);
		return;
	}
	g_free(key);

	bare = jabber_get_bare_jid(from);
	purple_debug_info(LOG, "signal xmpp2-marker account=%s peer=%s id=%s "
	                  "marker=%s\n", purple_account_get_username(account),
	                  bare, id, x2_marker_name(type));
	purple_signal_emit(signal_plugin, "xmpp2-marker", account, bare, id,
	                   x2_marker_name(type));
	if (type == X2_MARKER_DISPLAYED)
		x2_peer_notice(js, from, _("Message read."));
	else if (type == X2_MARKER_ACKNOWLEDGED)
		x2_peer_notice(js, from, _("Message acknowledged."));
	g_free(bare);
}

/* Remember the newest markable message of a contact (1:1 only). */
static void
remember_markable(JabberStream *js, const X2Msg *m, const char *id)
{
	GHashTable *peers = get_priv(js)->peers;
	PeerState *st;

	/* a MUC private message keeps the full JID in peer */
	if (m->peer == NULL || strchr(m->peer, '/') != NULL)
		return;

	st = g_hash_table_lookup(peers, m->peer);
	if (st == NULL) {
		st = g_new0(PeerState, 1);
		g_hash_table_insert(peers, g_strdup(m->peer), st);
	}
	/* catch-up delivers old messages after newer live ones */
	if (st->last_id != NULL && m->ts < st->last_ts)
		return;

	g_free(st->last_id);
	st->last_id = g_strdup(id);
	st->last_ts = m->ts;
}

static X2Verdict
markers_inbound(JabberStream *js, X2Msg *m)
{
	const char *from = xmlnode_get_attrib(m->msg, "from");
	X2MarkerType type;
	char *id;

	type = x2_marker_parse(m->msg, &id);
	if (type != X2_MARKER_NONE) {
		if (x2_msg_is_live_incoming(m) && from != NULL)
			got_marker(js, from, type, id);
		g_free(id);
		/* a pure marker is never shown, whatever its source */
		if (xmlnode_get_child(m->msg, "body") == NULL)
			return X2_CONSUMED;
	}

	/* live and archived messages from others; the other device that
	 * received a carbon marks it itself */
	if (m->dir == X2_DIR_IN && m->src != X2_SRC_CARBON) {
		id = x2_marker_markable_id(m->msg);
		if (id != NULL) {
			remember_markable(js, m, id);
			g_free(id);
		}
	}
	return X2_PASS;
}

const X2Ext x2_ext_markers = {
	.name = "markers",
	.plugin_init = markers_plugin_init,
	.plugin_uninit = markers_plugin_uninit,
	.stream_new = markers_stream_new,
	.stream_free = markers_stream_free,
	.inbound = markers_inbound,
	.outbound = markers_outbound,
};
