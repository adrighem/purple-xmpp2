/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file receipts.c XEP-0184 delivery receipts (ARCHITECTURE 7.4)
 *
 * Outgoing 1:1 messages ask for a receipt, live incoming requests from
 * contacts that can see our presence are answered, incoming receipts for
 * messages we asked about raise the "xmpp2-receipt" signal.
 */
#include "xmpp2_internal.h"

#include "account.h"
#include "accountopt.h"
#include "debug.h"
#include "signals.h"

#include "buddy.h"
#include "core/dedup.h"
#include "core/receipt_parse.h"
#include "ext/ext.h"
#include "ext/peerutil.h"
#include "jutil.h"
#include "namespaces.h"

#define LOG "xmpp2-receipts"

/* How many sent messages we remember to match receipts against */
#define SENT_CAPACITY 256

extern const X2Ext x2_ext_receipts;

static PurplePlugin *signal_plugin;

static void
receipts_plugin_init(PurplePlugin *plugin)
{
	signal_plugin = plugin;
	jabber_add_feature(NS_RECEIPTS, NULL);

	/* account, bare JID of the contact, id of the acknowledged message */
	purple_signal_register(plugin, "xmpp2-receipt",
		purple_marshal_VOID__POINTER_POINTER_POINTER, NULL, 3,
		purple_value_new(PURPLE_TYPE_SUBTYPE, PURPLE_SUBTYPE_ACCOUNT),
		purple_value_new(PURPLE_TYPE_STRING),
		purple_value_new(PURPLE_TYPE_STRING));
}

static void
receipts_plugin_uninit(PurplePlugin *plugin)
{
	jabber_remove_feature(NS_RECEIPTS);
	/* the signal itself goes with purple_signals_unregister_by_instance() */
	signal_plugin = NULL;
}

static void
receipts_account_options(GList **opts)
{
	*opts = g_list_append(*opts,
		purple_account_option_bool_new(_("Show delivery and read notices"),
		                               X2_PREF_NOTICES, FALSE));
}

static void
receipts_stream_new(JabberStream *js)
{
	x2_ext_set_priv(js, &x2_ext_receipts, x2_dedup_new(SENT_CAPACITY));
}

static void
receipts_stream_free(JabberStream *js)
{
	x2_dedup_free(x2_ext_get_priv(js, &x2_ext_receipts));
	x2_ext_set_priv(js, &x2_ext_receipts, NULL);
}

static void
receipts_outbound(JabberStream *js, xmlnode *message, X2Out *o)
{
	const char *to = xmlnode_get_attrib(message, "to");
	char *key;

	if (o->groupchat || !o->has_body || o->id == NULL || to == NULL)
		return;
	if (!x2_peer_wants(js, to, NS_RECEIPTS))
		return;

	x2_receipt_add_request(message);
	key = x2_ack_key(to, o->id);
	x2_dedup_add(x2_ext_get_priv(js, &x2_ext_receipts), key);
	g_free(key);
}

/* A contact acknowledges a message we asked about. */
static void
got_receipt(JabberStream *js, const char *from, const char *id)
{
	PurpleAccount *account = purple_connection_get_account(js->gc);
	char *key = x2_ack_key(from, id);
	char *bare;

	if (!x2_dedup_contains(x2_ext_get_priv(js, &x2_ext_receipts), key)) {
		purple_debug_info(LOG, "ignoring receipt %s from %s: we did not "
		                  "ask that contact\n", id, from);
		g_free(key);
		return;
	}
	g_free(key);

	bare = jabber_get_bare_jid(from);
	purple_debug_info(LOG, "signal xmpp2-receipt account=%s peer=%s id=%s\n",
	                  purple_account_get_username(account), bare, id);
	purple_signal_emit(signal_plugin, "xmpp2-receipt", account, bare, id);
	x2_peer_notice(js, from, _("Message delivered."));
	g_free(bare);
}

/*
 * Only contacts that see our presence get an answer: a receipt tells the
 * sender that we are online and reading (same policy as Gajim).
 */
static void
answer_request(JabberStream *js, const char *from, const char *id)
{
	char *bare = jabber_get_bare_jid(from);
	char *own = jabber_id_get_bare_jid(js->user);
	JabberBuddy *jb = jabber_buddy_find(js, bare, FALSE);
	xmlnode *received;

	if (purple_strequal(bare, own)) {
		purple_debug_info(LOG, "not answering our own message %s\n", id);
	} else if (jb == NULL || !(jb->subscription & JABBER_SUB_FROM)) {
		purple_debug_info(LOG, "not answering request %s from %s: "
		                  "no presence subscription\n", id, bare);
	} else {
		received = x2_receipt_new_received(from, id);
		jabber_send(js, received);
		xmlnode_free(received);
		purple_debug_info(LOG, "sent receipt for %s to %s\n", id, from);
	}
	g_free(own);
	g_free(bare);
}

static X2Verdict
receipts_inbound(JabberStream *js, X2Msg *m)
{
	const char *from = xmlnode_get_attrib(m->msg, "from");
	gboolean live = x2_msg_is_live_incoming(m);
	char *id;

	id = x2_receipt_received_id(m->msg);
	if (id != NULL) {
		if (live && from != NULL)
			got_receipt(js, from, id);
		g_free(id);
		/* a pure receipt is never shown, whatever its source */
		if (xmlnode_get_child(m->msg, "body") == NULL)
			return X2_CONSUMED;
	}

	/* never for archive results, carbons or delayed (offline) messages */
	if (live && from != NULL) {
		id = x2_receipt_request_id(m->msg);
		if (id != NULL) {
			answer_request(js, from, id);
			g_free(id);
		}
	}
	return X2_PASS;
}

const X2Ext x2_ext_receipts = {
	.name = "receipts",
	.plugin_init = receipts_plugin_init,
	.plugin_uninit = receipts_plugin_uninit,
	.account_options = receipts_account_options,
	.stream_new = receipts_stream_new,
	.stream_free = receipts_stream_free,
	.inbound = receipts_inbound,
	.outbound = receipts_outbound,
};
