/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/*
 * Unit tests of the correction module (ext/correct.c) inside the real
 * pipeline (ext/ext.c, ext/msgctx.c): the sender check, the marker, rooms,
 * and the outbound bookkeeping behind /correct.  Delivery is stubbed.
 */
#include <string.h>

#include "x2test.h"

#include "chat.h"
#include "core/replace_parse.h"
#include "ext/correct.h"
#include "ext/ext.h"
#include "ext/msgctx.h"
#include "jutil.h"
#include "message.h"

#define ME "alice@localhost"

const X2Ext *const x2_test_exts[] = { &x2_ext_correct, NULL };

/* ---- stubs ---- */

static struct {
	int count;
	char *body;
} delivered;

void
jabber_message_deliver(JabberStream *js, X2Msg *ctx)
{
	delivered.count++;
	g_free(delivered.body);
	delivered.body = xmlnode_get_data(xmlnode_get_child(ctx->msg, "body"));
}

JabberChat *
jabber_chat_find(JabberStream *js, const char *room, const char *server)
{
	return NULL;
}

void
jabber_add_feature(const char *namespace, JabberFeatureEnabled cb)
{
}

void
jabber_remove_feature(const char *namespace)
{
}

void
x2_correct_cmd_register(void)
{
}

void
x2_correct_cmd_unregister(void)
{
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
}

static void
teardown(Fixture *f, gconstpointer data)
{
	x2_ext_stream_free(f->js);
	jabber_id_free(f->js->user);
	g_free(f->js);
	g_free(delivered.body);
}

static void
receive(Fixture *f, const char *xml)
{
	xmlnode *n = xmlnode_from_str(xml, -1);

	g_assert_nonnull(n);
	x2_msg_receive(f->js, n);
	xmlnode_free(n);
}

#define CHAT(from, id, extra, body) \
	"<message xmlns='jabber:client' type='chat' from='" from "' " \
	"to='alice@localhost/desk' id='" id "'><body>" body "</body>" extra \
	"</message>"
#define REPLACE(id) "<replace xmlns='urn:xmpp:message-correct:0' id='" id "'/>"

/* ---- tests ---- */

static void
test_correction_is_marked(Fixture *f, gconstpointer data)
{
	receive(f, CHAT("bob@localhost/phone", "m1", "", "teh"));
	g_assert_cmpstr(delivered.body, ==, "teh");
	receive(f, CHAT("bob@localhost/laptop", "m2", REPLACE("m1"), "the"));
	g_assert_cmpint(delivered.count, ==, 2);
	g_assert_cmpstr(delivered.body, ==, X2_REPLACE_MARKER "the");
}

static void
test_second_correction_keeps_first_id(Fixture *f, gconstpointer data)
{
	receive(f, CHAT("bob@localhost/p", "m1", "", "a"));
	receive(f, CHAT("bob@localhost/p", "m2", REPLACE("m1"), "b"));
	receive(f, CHAT("bob@localhost/p", "m3", REPLACE("m1"), "c"));
	g_assert_cmpstr(delivered.body, ==, X2_REPLACE_MARKER "c");
}

/* carol's message id referenced by bob: plain message */
static void
test_foreign_id_not_marked(Fixture *f, gconstpointer data)
{
	receive(f, CHAT("carol@localhost/p", "c1", "", "carol says"));
	receive(f, CHAT("bob@localhost/p", "m2", REPLACE("c1"), "forged"));
	g_assert_cmpint(delivered.count, ==, 2);
	g_assert_cmpstr(delivered.body, ==, "forged");
}

static void
test_unknown_original_not_marked(Fixture *f, gconstpointer data)
{
	receive(f, CHAT("bob@localhost/p", "m2", REPLACE("never-seen"), "x"));
	g_assert_cmpint(delivered.count, ==, 1);
	g_assert_cmpstr(delivered.body, ==, "x");
}

#define GROUP(nick, id, extra, body) \
	"<message xmlns='jabber:client' type='groupchat' " \
	"from='room@conference.localhost/" nick "' id='" id "'>" \
	"<body>" body "</body>" extra "</message>"

static void
test_room_same_nick_only(Fixture *f, gconstpointer data)
{
	receive(f, GROUP("bob", "g1", "", "teh"));
	receive(f, GROUP("mallory", "g2", REPLACE("g1"), "evil"));
	g_assert_cmpstr(delivered.body, ==, "evil");
	receive(f, GROUP("bob", "g3", REPLACE("g1"), "the"));
	g_assert_cmpstr(delivered.body, ==, X2_REPLACE_MARKER "the");
}

#define OCC(id) "<occupant-id xmlns='urn:xmpp:occupant-id:0' id='" id "'/>"

static void
test_room_occupant_id_survives_nick_change(Fixture *f, gconstpointer data)
{
	receive(f, GROUP("bob", "g1", OCC("O1"), "teh"));
	receive(f, GROUP("robert", "g2", REPLACE("g1") OCC("O1"), "the"));
	g_assert_cmpstr(delivered.body, ==, X2_REPLACE_MARKER "the");
	/* another occupant that took the old nick is not the author */
	receive(f, GROUP("bob", "g3", REPLACE("g1") OCC("O2"), "evil"));
	g_assert_cmpstr(delivered.body, ==, "evil");
}

static void
test_xhtml_dropped_so_marker_shows(Fixture *f, gconstpointer data)
{
	receive(f, CHAT("bob@localhost/p", "m1", "", "a"));
	receive(f, CHAT("bob@localhost/p", "m2",
		REPLACE("m1") "<html xmlns='http://jabber.org/protocol/xhtml-im'>"
		"<body xmlns='http://www.w3.org/1999/xhtml'>b</body></html>", "b"));
	g_assert_cmpstr(delivered.body, ==, X2_REPLACE_MARKER "b");
}

static void
test_outbound_tracks_last_sent_and_pending(Fixture *f, gconstpointer data)
{
	X2CorrectState *st = x2_correct_state(f->js);
	xmlnode *m = xmlnode_from_str("<message type='chat' "
		"to='bob@localhost/phone'><body>teh</body></message>", -1);
	char *id1, *id2;

	g_assert_nonnull(st);
	x2_msg_prepare_outgoing(f->js, m);
	id1 = g_strdup(xmlnode_get_attrib(m, "id"));
	g_assert_cmpstr(x2_corrlog_last_sent(st->log, "bob@localhost"), ==, id1);
	g_assert_null(x2_replace_id(m));
	xmlnode_free(m);

	/* /correct: pending set, the next message carries the replace and is
	 * not itself the message to correct next time */
	m = xmlnode_from_str("<message type='chat' to='bob@localhost'>"
		"<body>the</body></message>", -1);
	st->pending = g_strdup(id1);
	x2_msg_prepare_outgoing(f->js, m);
	id2 = x2_replace_id(m);
	g_assert_cmpstr(id2, ==, id1);
	g_assert_cmpstr(x2_corrlog_last_sent(st->log, "bob@localhost"), ==, id1);
	g_free(id2);
	g_free(id1);
	xmlnode_free(m);
}

static void
test_outbound_without_body_ignored(Fixture *f, gconstpointer data)
{
	X2CorrectState *st = x2_correct_state(f->js);
	xmlnode *m = xmlnode_from_str("<message type='chat' to='bob@localhost'>"
		"<composing xmlns='http://jabber.org/protocol/chatstates'/>"
		"</message>", -1);

	x2_msg_prepare_outgoing(f->js, m);
	g_assert_null(x2_corrlog_last_sent(st->log, "bob@localhost"));
	xmlnode_free(m);
}

int
main(int argc, char **argv)
{
	x2_test_init(&argc, &argv);
#define T(name, fn) g_test_add("/correct/" name, Fixture, NULL, setup, fn, teardown)
	T("marked", test_correction_is_marked);
	T("repeat", test_second_correction_keeps_first_id);
	T("foreign-id", test_foreign_id_not_marked);
	T("unknown-original", test_unknown_original_not_marked);
	T("room-nick", test_room_same_nick_only);
	T("room-occupant-id", test_room_occupant_id_survives_nick_change);
	T("xhtml", test_xhtml_dropped_so_marker_shows);
	T("outbound", test_outbound_tracks_last_sent_and_pending);
	T("outbound-no-body", test_outbound_without_body_ignored);
	return g_test_run();
}
