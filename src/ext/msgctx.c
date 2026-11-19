/**
 * @file msgctx.c X2Msg: build, classify and deliver inbound messages
 */
#include "xmpp2_internal.h"

#include "conversation.h"
#include "debug.h"

#include "core/forward.h"
#include "core/sid.h"
#include "ext/ext.h"
#include "ext/msgctx.h"
#include "jutil.h"
#include "message.h"

/*
 * The conversation key of a message: the bare JID, except for a private
 * message from a room occupant, which keeps the full JID so that the reply
 * goes to the occupant and not to the room.
 */
static char *
make_peer(JabberStream *js, const char *jid, const char *type)
{
	JabberID *id;
	char *peer;

	if (jid == NULL)
		return NULL;

	id = jabber_id_new(jid);
	if (id == NULL)
		return g_strdup(jid);

	if (id->resource != NULL && id->node != NULL &&
	    !purple_strequal(type, "groupchat") &&
	    jabber_chat_find(js, id->node, id->domain) != NULL)
		peer = g_strdup(jid);
	else
		peer = jabber_id_get_bare_jid(id);

	jabber_id_free(id);
	return peer;
}

static void
update_from_message(JabberStream *js, X2Msg *m)
{
	const char *type = xmlnode_get_attrib(m->msg, "type");
	const char *jid = m->dir == X2_DIR_OUT ? xmlnode_get_attrib(m->msg, "to")
	                                       : xmlnode_get_attrib(m->msg, "from");
	char *origin_id;

	g_free(m->peer);
	m->peer = make_peer(js, jid, type);

	g_free(m->stanza_id);
	m->stanza_id = m->archive ? x2_sid_stanza_id(m->msg, m->archive) : NULL;

	origin_id = x2_sid_origin_id(m->msg);
	g_free(m->key);
	m->key = x2_dedup_make_key(m->stanza_id, m->dir == X2_DIR_OUT, m->peer,
	                           origin_id, xmlnode_get_attrib(m->msg, "id"));
	g_free(origin_id);

	m->delayed = x2_delay_parse(m->msg, &m->ts, NULL);
	if (!m->delayed)
		m->ts = time(NULL);
}

X2Msg *
x2_msg_new(JabberStream *js, xmlnode *packet)
{
	X2Msg *m = g_new0(X2Msg, 1);

	m->outer = packet;
	m->msg = packet;
	m->src = X2_SRC_LIVE;
	m->dir = X2_DIR_IN;
	update_from_message(js, m);
	return m;
}

void
x2_msg_free(X2Msg *m)
{
	if (m == NULL)
		return;
	g_free(m->archive);
	g_free(m->stanza_id);
	g_free(m->key);
	g_free(m->peer);
	g_free(m);
}

void
x2_msg_set_inner(JabberStream *js, X2Msg *m, xmlnode *inner,
                 X2Source src, X2Dir dir, const char *archive)
{
	g_return_if_fail(inner != NULL);

	m->msg = inner;
	m->src = src;
	m->dir = dir;
	g_free(m->archive);
	m->archive = g_strdup(archive);
	update_from_message(js, m);
}

void
x2_msg_receive(JabberStream *js, xmlnode *packet)
{
	X2Msg *m = x2_msg_new(js, packet);

	if (x2_ext_inbound(js, m) == X2_PASS)
		jabber_message_deliver(js, m);

	x2_msg_free(m);
}

gboolean
x2_msg_is_live_incoming(const X2Msg *m)
{
	return m == NULL ||
	       (m->src == X2_SRC_LIVE && m->dir == X2_DIR_IN && !m->delayed);
}

void
x2_msg_write_outgoing(JabberStream *js, const X2Msg *m, const char *body)
{
	PurpleAccount *account = purple_connection_get_account(js->gc);
	PurpleConversation *conv;

	g_return_if_fail(m != NULL && m->peer != NULL && body != NULL);

	conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM,
	                                             m->peer, account);
	if (conv == NULL)
		conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account,
		                               m->peer);

	purple_conv_im_write(PURPLE_CONV_IM(conv), purple_account_get_username(account),
	                     body,
	                     PURPLE_MESSAGE_SEND | PURPLE_MESSAGE_REMOTE_SEND,
	                     m->ts);
}

void
x2_msg_prepare_outgoing(JabberStream *js, xmlnode *message)
{
	const char *type = xmlnode_get_attrib(message, "type");
	X2Out out;
	char *id;

	if (purple_strequal(type, "error"))
		return;

	/* One id serves as stanza id and origin-id: the server echoes the
	 * stanza id back in receipts, corrections and archive results. */
	id = x2_sid_new_id();
	xmlnode_set_attrib(message, "id", id);
	x2_sid_add_origin_id(message, id);

	out.groupchat = purple_strequal(type, "groupchat");
	out.has_body = xmlnode_get_child(message, "body") != NULL;
	out.id = id;
	x2_ext_outbound(js, message, &out);

	g_free(id);
}
