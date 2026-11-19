/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/* Unit tests of core/receipt_parse.c: XEP-0184 receipts, XEP-0333 markers. */
#include <glib.h>

#include "x2test.h"

#include "receipt_parse.h"

#define RCPT "xmlns='urn:xmpp:receipts'"
#define MARK "xmlns='urn:xmpp:chat-markers:0'"

static xmlnode *
parse(const char *xml)
{
	xmlnode *n = xmlnode_from_str(xml, -1);

	g_assert_nonnull(n);
	return n;
}

static void
check_request(const char *xml, const char *expected)
{
	xmlnode *m = parse(xml);
	char *id = x2_receipt_request_id(m);

	g_assert_cmpstr(id, ==, expected);
	g_free(id);
	xmlnode_free(m);
}

static void
test_request_id(void)
{
	check_request("<message type='chat' id='m1'><body>x</body>"
	              "<request " RCPT "/></message>", "m1");
	/* type is optional, normal is fine */
	check_request("<message id='m1'><request " RCPT "/></message>", "m1");
	check_request("<message type='normal' id='m1'><request " RCPT "/>"
	              "</message>", "m1");
}

static void
test_request_refused(void)
{
	/* no id, empty id */
	check_request("<message type='chat'><request " RCPT "/></message>", NULL);
	check_request("<message type='chat' id=''><request " RCPT "/></message>",
	              NULL);
	/* wrong type */
	check_request("<message type='groupchat' id='m1'><request " RCPT "/>"
	              "</message>", NULL);
	check_request("<message type='error' id='m1'><request " RCPT "/>"
	              "</message>", NULL);
	check_request("<message type='headline' id='m1'><request " RCPT "/>"
	              "</message>", NULL);
	/* wrong namespace, no request */
	check_request("<message type='chat' id='m1'>"
	              "<request xmlns='urn:other'/></message>", NULL);
	check_request("<message type='chat' id='m1'><body>x</body></message>",
	              NULL);
	/* a receipt is never answered, even with a request next to it */
	check_request("<message type='chat' id='m1'><received " RCPT " id='a'/>"
	              "<request " RCPT "/></message>", NULL);
}

static void
test_received_id(void)
{
	xmlnode *m = parse("<message><received " RCPT " id='abc'/></message>");
	char *id = x2_receipt_received_id(m);

	g_assert_cmpstr(id, ==, "abc");
	g_free(id);
	xmlnode_free(m);

	m = parse("<message><received " RCPT "/></message>");
	g_assert_null(x2_receipt_received_id(m));
	xmlnode_free(m);

	m = parse("<message><received xmlns='urn:other' id='abc'/></message>");
	g_assert_null(x2_receipt_received_id(m));
	xmlnode_free(m);
}

static void
test_build_receipt(void)
{
	xmlnode *m = x2_receipt_new_received("bob@x/r", "m1");
	xmlnode *r = xmlnode_get_child_with_namespace(m, "received",
	                                              "urn:xmpp:receipts");
	char *id;

	g_assert_cmpstr(xmlnode_get_attrib(m, "to"), ==, "bob@x/r");
	g_assert_nonnull(r);
	g_assert_cmpstr(xmlnode_get_attrib(r, "id"), ==, "m1");
	/* and it round-trips through the parser */
	id = x2_receipt_received_id(m);
	g_assert_cmpstr(id, ==, "m1");
	g_free(id);
	xmlnode_free(m);
}

static void
test_add_request_and_markable(void)
{
	xmlnode *m = parse("<message type='chat' id='m1'><body>x</body></message>");
	char *id;

	x2_receipt_add_request(m);
	x2_marker_add_markable(m);
	id = x2_receipt_request_id(m);
	g_assert_cmpstr(id, ==, "m1");
	g_free(id);
	id = x2_marker_markable_id(m);
	g_assert_cmpstr(id, ==, "m1");
	g_free(id);
	xmlnode_free(m);
}

static void
test_markable_refused(void)
{
	xmlnode *m = parse("<message type='groupchat' id='m1'>"
	                   "<markable " MARK "/></message>");

	g_assert_null(x2_marker_markable_id(m));
	xmlnode_free(m);

	m = parse("<message type='chat'><markable " MARK "/></message>");
	g_assert_null(x2_marker_markable_id(m));
	xmlnode_free(m);

	m = parse("<message type='chat' id='m1'><markable xmlns='urn:x'/>"
	          "</message>");
	g_assert_null(x2_marker_markable_id(m));
	xmlnode_free(m);
}

static void
test_marker_parse(void)
{
	static const struct { const char *name; X2MarkerType type; } cases[] = {
		{ "received", X2_MARKER_RECEIVED },
		{ "displayed", X2_MARKER_DISPLAYED },
		{ "acknowledged", X2_MARKER_ACKNOWLEDGED },
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS(cases); i++) {
		char *xml = g_strdup_printf("<message><%s " MARK " id='m9'/>"
		                            "</message>", cases[i].name);
		xmlnode *m = parse(xml);
		char *id = NULL;

		g_assert_cmpint(x2_marker_parse(m, &id), ==, cases[i].type);
		g_assert_cmpstr(id, ==, "m9");
		g_assert_cmpstr(x2_marker_name(cases[i].type), ==, cases[i].name);
		g_free(id);
		xmlnode_free(m);
		g_free(xml);
	}
}

static void
test_marker_parse_none(void)
{
	char *id = NULL;
	xmlnode *m = parse("<message><displayed xmlns='urn:other' id='m9'/>"
	                   "</message>");

	g_assert_cmpint(x2_marker_parse(m, &id), ==, X2_MARKER_NONE);
	xmlnode_free(m);

	m = parse("<message><displayed " MARK "/></message>");
	g_assert_cmpint(x2_marker_parse(m, &id), ==, X2_MARKER_NONE);
	g_assert_null(id);
	xmlnode_free(m);

	/* markable is not a marker */
	m = parse("<message id='m1'><markable " MARK "/></message>");
	g_assert_cmpint(x2_marker_parse(m, &id), ==, X2_MARKER_NONE);
	xmlnode_free(m);

	g_assert_null(x2_marker_name(X2_MARKER_NONE));
}

static void
test_build_marker(void)
{
	xmlnode *m = x2_marker_new("bob@x", X2_MARKER_DISPLAYED, "m1");
	char *id = NULL;

	g_assert_cmpstr(xmlnode_get_attrib(m, "to"), ==, "bob@x");
	g_assert_cmpstr(xmlnode_get_attrib(m, "type"), ==, "chat");
	g_assert_cmpint(x2_marker_parse(m, &id), ==, X2_MARKER_DISPLAYED);
	g_assert_cmpstr(id, ==, "m1");
	g_free(id);
	xmlnode_free(m);

	g_test_expect_message(NULL, G_LOG_LEVEL_CRITICAL, "*");
	g_assert_null(x2_marker_new("bob@x", X2_MARKER_NONE, "m1"));
	g_test_assert_expected_messages();
}

static void
check_key(const char *jid, const char *id, const char *expected)
{
	char *key = x2_ack_key(jid, id);

	g_assert_cmpstr(key, ==, expected);
	g_free(key);
}

static void
test_ack_key(void)
{
	/* the resource and the case do not matter, the contact and id do */
	check_key("bob@x", "m1", "bob@x\x1fm1");
	check_key("Bob@X/phone", "m1", "bob@x\x1fm1");
	check_key("carol@x", "m1", "carol@x\x1fm1");
	check_key("bob@x", "m2", "bob@x\x1fm2");
}

int
main(int argc, char **argv)
{
	x2_test_init(&argc, &argv);
	g_test_add_func("/receipt/request_id", test_request_id);
	g_test_add_func("/receipt/request_refused", test_request_refused);
	g_test_add_func("/receipt/received_id", test_received_id);
	g_test_add_func("/receipt/build", test_build_receipt);
	g_test_add_func("/receipt/add_request_and_markable",
	                test_add_request_and_markable);
	g_test_add_func("/marker/markable_refused", test_markable_refused);
	g_test_add_func("/marker/parse", test_marker_parse);
	g_test_add_func("/marker/parse_none", test_marker_parse_none);
	g_test_add_func("/marker/build", test_build_marker);
	g_test_add_func("/receipt/ack_key", test_ack_key);
	return g_test_run();
}
