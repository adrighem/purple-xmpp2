/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/*
 * Unit tests of the carbons module (ext/carbons.c) inside the real pipeline
 * (ext/ext.c, ext/msgctx.c): trusted support detection, the enable IQ,
 * unwrapping into the right conversation and direction, and that forged
 * carbons never reach delivery.  IQ sending and delivery are stubbed.
 */
#include "x2test.h"

#include "chat.h"
#include "ext/ext.h"
#include "ext/msgctx.h"
#include "iq.h"
#include "jutil.h"
#include "message.h"

#define ME "alice@localhost"

extern const X2Ext x2_ext_carbons;

const X2Ext *const x2_test_exts[] = { &x2_ext_carbons, NULL };

/* ---- stubs ---- */

static struct {
	int count;
	char *body;
	char *peer;
	X2Source src;
	X2Dir dir;
} delivered;

static int iq_sent;
static JabberIqCallback *iq_cb;

void
jabber_message_deliver(JabberStream *js, X2Msg *ctx)
{
	delivered.count++;
	g_free(delivered.body);
	delivered.body = xmlnode_get_data(xmlnode_get_child(ctx->msg, "body"));
	g_free(delivered.peer);
	delivered.peer = g_strdup(ctx->peer);
	delivered.src = ctx->src;
	delivered.dir = ctx->dir;
}

JabberChat *
jabber_chat_find(JabberStream *js, const char *room, const char *server)
{
	return NULL;
}

JabberIq *
jabber_iq_new(JabberStream *js, JabberIqType type)
{
	JabberIq *iq = g_new0(JabberIq, 1);

	iq->type = type;
	iq->js = js;
	iq->node = xmlnode_new("iq");
	return iq;
}

void
jabber_iq_set_callback(JabberIq *iq, JabberIqCallback *cb, gpointer data)
{
	iq_cb = cb;
}

void
jabber_iq_send(JabberIq *iq)
{
	xmlnode *enable = xmlnode_get_child(iq->node, "enable");

	g_assert_cmpint(iq->type, ==, JABBER_IQ_SET);
	g_assert_nonnull(enable);
	g_assert_cmpstr(xmlnode_get_namespace(enable), ==, "urn:xmpp:carbons:2");
	iq_sent++;
	xmlnode_free(iq->node);
	g_free(iq);
}

/* ---- fixture ---- */

typedef struct {
	JabberStream *js;
} Fixture;

static void
setup(Fixture *f, gconstpointer data)
{
	f->js = g_new0(JabberStream, 1);
	f->js->user = jabber_id_new(ME "/desk");
	x2_ext_stream_new(f->js);
	memset(&delivered, 0, sizeof(delivered));
	iq_sent = 0;
	iq_cb = NULL;
}

static void
teardown(Fixture *f, gconstpointer data)
{
	x2_ext_stream_free(f->js);
	jabber_id_free(f->js->user);
	g_free(f->js);
	g_free(delivered.body);
	g_free(delivered.peer);
}

static void
server_info(Fixture *f, const char *from, const char *feature)
{
	char *xml = g_strdup_printf(
		"<query xmlns='http://jabber.org/protocol/disco#info'>"
		"<feature var='%s'/></query>", feature);
	xmlnode *q = xmlnode_from_str(xml, -1);

	g_assert_nonnull(q);
	x2_ext_server_info(f->js, from, q);
	xmlnode_free(q);
	g_free(xml);
}

static void
receive(Fixture *f, const char *xml)
{
	xmlnode *n = xmlnode_from_str(xml, -1);

	g_assert_nonnull(n);
	x2_msg_receive(f->js, n);
	xmlnode_free(n);
}

#define CARBON(from, wrapper, inner) \
	"<message xmlns='jabber:client' from='" from "' " \
	"to='alice@localhost/desk'><" wrapper " xmlns='urn:xmpp:carbons:2'>" \
	"<forwarded xmlns='urn:xmpp:forward:0'>" \
	"<message xmlns='jabber:client' " inner "><body>hi</body></message>" \
	"</forwarded></" wrapper "></message>"

/* ---- tests ---- */

static void
test_enable_after_session_ready(Fixture *f, gconstpointer data)
{
	server_info(f, "localhost", "urn:xmpp:carbons:2");
	x2_ext_session_ready(f->js);
	g_assert_cmpint(iq_sent, ==, 1);
	g_assert_nonnull(iq_cb);
	/* result and error both settle; calling twice stays harmless */
	iq_cb(f->js, NULL, JABBER_IQ_ERROR, "1", NULL, NULL);
	iq_cb(f->js, NULL, JABBER_IQ_RESULT, "1", NULL, NULL);
}

static void
test_no_enable_without_support(Fixture *f, gconstpointer data)
{
	server_info(f, "localhost", "urn:xmpp:mam:2");
	x2_ext_session_ready(f->js);
	g_assert_cmpint(iq_sent, ==, 0);
}

/* A component of our server advertising carbons is not our server */
static void
test_component_carbons_ignored(Fixture *f, gconstpointer data)
{
	x2_ext_server_item_add(f->js, "upload.localhost");
	server_info(f, "upload.localhost", "urn:xmpp:carbons:2");
	x2_ext_session_ready(f->js);
	g_assert_cmpint(iq_sent, ==, 0);
}

static void
test_own_bare_jid_advertises(Fixture *f, gconstpointer data)
{
	server_info(f, ME, "urn:xmpp:carbons:2");
	x2_ext_session_ready(f->js);
	g_assert_cmpint(iq_sent, ==, 1);
}

static void
test_sent_carbon_is_outgoing_to_peer(Fixture *f, gconstpointer data)
{
	receive(f, CARBON("alice@localhost", "sent",
		"from='alice@localhost/phone' to='bob@localhost/x' type='chat'"));
	g_assert_cmpint(delivered.count, ==, 1);
	g_assert_cmpint(delivered.src, ==, X2_SRC_CARBON);
	g_assert_cmpint(delivered.dir, ==, X2_DIR_OUT);
	g_assert_cmpstr(delivered.peer, ==, "bob@localhost");
	g_assert_cmpstr(delivered.body, ==, "hi");
}

static void
test_received_carbon_is_incoming_from_peer(Fixture *f, gconstpointer data)
{
	receive(f, CARBON("alice@localhost", "received",
		"from='bob@localhost/x' to='alice@localhost/phone' type='chat'"));
	g_assert_cmpint(delivered.count, ==, 1);
	g_assert_cmpint(delivered.src, ==, X2_SRC_CARBON);
	g_assert_cmpint(delivered.dir, ==, X2_DIR_IN);
	g_assert_cmpstr(delivered.peer, ==, "bob@localhost");
}

static void
expect_forged(Fixture *f, const char *xml)
{
	receive(f, xml);
	g_assert_cmpint(delivered.count, ==, 0);
}

static void
test_forged_carbons_not_delivered(Fixture *f, gconstpointer data)
{
	/* 1: a contact wraps a <sent/> */
	expect_forged(f, CARBON("bob@localhost", "sent",
		"from='alice@localhost/phone' to='bob@localhost' type='chat'"));
	/* 2: our bare JID outside, foreign inner from */
	expect_forged(f, CARBON("alice@localhost", "sent",
		"from='bob@localhost/x' to='carol@localhost' type='chat'"));
	/* 3: our bare JID outside, inner to somebody else */
	expect_forged(f, CARBON("alice@localhost", "received",
		"from='bob@localhost/x' to='carol@localhost' type='chat'"));
}

static void
test_plain_message_passes(Fixture *f, gconstpointer data)
{
	receive(f, "<message xmlns='jabber:client' from='bob@localhost/x' "
		"to='alice@localhost/desk' type='chat'><body>hi</body></message>");
	g_assert_cmpint(delivered.count, ==, 1);
	g_assert_cmpint(delivered.src, ==, X2_SRC_LIVE);
	g_assert_cmpint(delivered.dir, ==, X2_DIR_IN);
}

int
main(int argc, char **argv)
{
	x2_test_init(&argc, &argv);

#define T(name, fn) g_test_add("/carbons/" name, Fixture, NULL, setup, fn, teardown)
	T("enable-after-session-ready", test_enable_after_session_ready);
	T("no-enable-without-support", test_no_enable_without_support);
	T("component-carbons-ignored", test_component_carbons_ignored);
	T("own-bare-jid-advertises", test_own_bare_jid_advertises);
	T("sent-is-outgoing", test_sent_carbon_is_outgoing_to_peer);
	T("received-is-incoming", test_received_carbon_is_incoming_from_peer);
	T("forged-not-delivered", test_forged_carbons_not_delivered);
	T("plain-message-passes", test_plain_message_passes);
	return g_test_run();
}
