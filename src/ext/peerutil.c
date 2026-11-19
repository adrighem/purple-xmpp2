/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file peerutil.c Helpers shared by the receipts and markers modules
 */
#include "xmpp2_internal.h"

#include "account.h"
#include "conversation.h"

#include "buddy.h"
#include "chat.h"
#include "ext/peerutil.h"
#include "jutil.h"

gboolean
x2_peer_is_occupant(JabberStream *js, const char *jid)
{
	JabberID *id = jabber_id_new(jid);
	gboolean occupant;

	if (id == NULL)
		return FALSE;
	occupant = id->node != NULL &&
	           jabber_chat_find(js, id->node, id->domain) != NULL;
	jabber_id_free(id);
	return occupant;
}

gboolean
x2_peer_sees_us(JabberStream *js, const char *jid)
{
	char *bare = jabber_get_bare_jid(jid);
	JabberBuddy *jb = bare ? jabber_buddy_find(js, bare, FALSE) : NULL;
	gboolean sees = jb != NULL && (jb->subscription & JABBER_SUB_FROM);

	g_free(bare);
	return sees;
}

gboolean
x2_peer_wants(JabberStream *js, const char *jid, const char *ns)
{
	JabberID *id;
	JabberBuddy *jb;
	JabberBuddyResource *jbr;
	char *bare;
	gboolean wants = TRUE;

	if (x2_peer_is_occupant(js, jid))
		return FALSE;

	id = jabber_id_new(jid);
	if (id == NULL)
		return FALSE;
	bare = jabber_id_get_bare_jid(id);
	jb = jabber_buddy_find(js, bare, FALSE);
	jbr = jb ? jabber_buddy_find_resource(jb, id->resource) : NULL;
	if (jbr != NULL && jbr->caps.info != NULL)
		wants = jabber_resource_has_capability(jbr, ns);
	g_free(bare);
	jabber_id_free(id);
	return wants;
}

void
x2_peer_notice(JabberStream *js, const char *jid, const char *text)
{
	PurpleAccount *account = purple_connection_get_account(js->gc);
	PurpleConversation *conv;
	char *bare;

	if (!purple_account_get_bool(account, X2_PREF_NOTICES, FALSE))
		return;

	/* The conversation is named after the full JID once the contact has
	 * written to us, else after the bare one. */
	conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, jid,
	                                             account);
	if (conv == NULL) {
		bare = jabber_get_bare_jid(jid);
		conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM,
		                                             bare, account);
		g_free(bare);
	}
	if (conv == NULL)
		return;

	purple_conversation_write(conv, NULL, text,
	                          PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_NO_LOG,
	                          time(NULL));
}
