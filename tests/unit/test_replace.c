/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/* Unit tests of core/replace_parse.c and core/corrlog.c. */
#include <string.h>
#include <glib.h>

#include "x2test.h"

#include "corrlog.h"
#include "replace_parse.h"

#define MSG_OPEN "<message xmlns='jabber:client' from='bob@x/r' type='chat'>"

static xmlnode *
parse(const char *xml)
{
	xmlnode *n = xmlnode_from_str(xml, -1);

	g_assert_nonnull(n);
	return n;
}

static void
test_replace_id(void)
{
	xmlnode *m = parse(MSG_OPEN "<body>x</body>"
		"<replace xmlns='urn:xmpp:message-correct:0' id='m1'/></message>");
	char *id = x2_replace_id(m);

	g_assert_cmpstr(id, ==, "m1");
	g_free(id);
	xmlnode_free(m);
}

static void
test_replace_id_absent_wrong_ns_empty(void)
{
	xmlnode *m = parse(MSG_OPEN "<replace xmlns='urn:other' id='m1'/>"
		"<replace xmlns='urn:xmpp:message-correct:0' id=''/></message>");

	g_assert_null(x2_replace_id(m));
	xmlnode_free(m);
	m = parse(MSG_OPEN "<body>x</body></message>");
	g_assert_null(x2_replace_id(m));
	xmlnode_free(m);
}

static void
test_replace_add_roundtrip(void)
{
	xmlnode *m = parse(MSG_OPEN "<body>x</body></message>");
	char *id;

	x2_replace_add(m, "orig-7");
	id = x2_replace_id(m);
	g_assert_cmpstr(id, ==, "orig-7");
	g_free(id);
	xmlnode_free(m);
}

static void
test_sender_identity(void)
{
	xmlnode *plain = parse(MSG_OPEN "<body>x</body></message>");
	xmlnode *occ = parse(MSG_OPEN "<occupant-id xmlns='urn:xmpp:occupant-id:0'"
		" id='O1'/></message>");
	char *s;

	s = x2_replace_sender(plain, "bob@x/phone", FALSE);
	g_assert_cmpstr(s, ==, "bob@x");
	g_free(s);
	s = x2_replace_sender(plain, "room@c/bob", TRUE);
	g_assert_cmpstr(s, ==, "room@c/bob");
	g_free(s);
	s = x2_replace_sender(occ, "room@c/bob", TRUE);
	g_assert_cmpstr(s, ==, "occ:O1");
	g_free(s);
	/* an occupant-id means nothing outside a room */
	s = x2_replace_sender(occ, "bob@x/r", FALSE);
	g_assert_cmpstr(s, ==, "bob@x");
	g_free(s);
	g_assert_null(x2_replace_sender(plain, NULL, FALSE));
	xmlnode_free(plain);
	xmlnode_free(occ);
}

static void
test_mark_body(void)
{
	xmlnode *m = parse(MSG_OPEN "<body>the</body>"
		"<html xmlns='http://jabber.org/protocol/xhtml-im'>"
		"<body xmlns='http://www.w3.org/1999/xhtml'>the</body></html>"
		"</message>");
	xmlnode *body;
	char *t;

	g_assert_true(x2_replace_mark_body(m));
	body = xmlnode_get_child(m, "body");
	g_assert_nonnull(body);
	g_assert_cmpstr(xmlnode_get_namespace(body), ==, "jabber:client");
	t = xmlnode_get_data(body);
	g_assert_cmpstr(t, ==, X2_REPLACE_MARKER "the");
	g_free(t);
	g_assert_null(xmlnode_get_child_with_namespace(m, "html",
		"http://jabber.org/protocol/xhtml-im"));
	xmlnode_free(m);
}

static void
test_mark_body_needs_body(void)
{
	xmlnode *m = parse(MSG_OPEN "<body></body></message>");

	g_assert_false(x2_replace_mark_body(m));
	xmlnode_free(m);
	m = parse(MSG_OPEN "<active xmlns='http://jabber.org/protocol/chatstates'/>"
		"</message>");
	g_assert_false(x2_replace_mark_body(m));
	xmlnode_free(m);
}

static void
test_corrlog_verdicts(void)
{
	X2CorrLog *l = x2_corrlog_new(8);

	x2_corrlog_record(l, "bob@x", "m1", "bob@x");
	g_assert_cmpint(x2_corrlog_check(l, "bob@x", "m1", "bob@x"), ==, X2_CORR_OK);
	g_assert_cmpint(x2_corrlog_check(l, "bob@x", "m1", "carol@x"), ==,
	                X2_CORR_MISMATCH);
	g_assert_cmpint(x2_corrlog_check(l, "bob@x", "m2", "bob@x"), ==,
	                X2_CORR_UNKNOWN);
	/* the same id in another conversation is another message */
	g_assert_cmpint(x2_corrlog_check(l, "carol@x", "m1", "bob@x"), ==,
	                X2_CORR_UNKNOWN);
	/* a later claim on a known id does not take it over */
	x2_corrlog_record(l, "bob@x", "m1", "carol@x");
	g_assert_cmpint(x2_corrlog_check(l, "bob@x", "m1", "bob@x"), ==, X2_CORR_OK);
	x2_corrlog_free(l);
}

static void
test_corrlog_bounded(void)
{
	X2CorrLog *l = x2_corrlog_new(3);
	int i;

	for (i = 0; i < 5; i++) {
		char *id = g_strdup_printf("m%d", i);

		x2_corrlog_record(l, "p", id, "s");
		g_free(id);
	}
	g_assert_cmpint(x2_corrlog_check(l, "p", "m0", "s"), ==, X2_CORR_UNKNOWN);
	g_assert_cmpint(x2_corrlog_check(l, "p", "m1", "s"), ==, X2_CORR_UNKNOWN);
	g_assert_cmpint(x2_corrlog_check(l, "p", "m2", "s"), ==, X2_CORR_OK);
	g_assert_cmpint(x2_corrlog_check(l, "p", "m4", "s"), ==, X2_CORR_OK);
	x2_corrlog_free(l);
}

static void
test_corrlog_last_sent(void)
{
	X2CorrLog *l = x2_corrlog_new(4);

	g_assert_null(x2_corrlog_last_sent(l, "bob@x"));
	x2_corrlog_set_last_sent(l, "bob@x", "a");
	x2_corrlog_set_last_sent(l, "bob@x", "b");
	x2_corrlog_set_last_sent(l, "carol@x", "c");
	g_assert_cmpstr(x2_corrlog_last_sent(l, "bob@x"), ==, "b");
	g_assert_cmpstr(x2_corrlog_last_sent(l, "carol@x"), ==, "c");
	x2_corrlog_free(l);
}

int
main(int argc, char **argv)
{
	x2_test_init(&argc, &argv);
	g_test_add_func("/replace/id", test_replace_id);
	g_test_add_func("/replace/id-absent", test_replace_id_absent_wrong_ns_empty);
	g_test_add_func("/replace/add", test_replace_add_roundtrip);
	g_test_add_func("/replace/sender", test_sender_identity);
	g_test_add_func("/replace/mark", test_mark_body);
	g_test_add_func("/replace/mark-needs-body", test_mark_body_needs_body);
	g_test_add_func("/corrlog/verdicts", test_corrlog_verdicts);
	g_test_add_func("/corrlog/bounded", test_corrlog_bounded);
	g_test_add_func("/corrlog/last-sent", test_corrlog_last_sent);
	return g_test_run();
}
