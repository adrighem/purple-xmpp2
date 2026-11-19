/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/* Unit tests of core/forward.c: <forwarded/> unwrap and delay parsing. */
#include <glib.h>

#include "x2test.h"

#include "forward.h"

static xmlnode *
parse(const char *xml)
{
	xmlnode *n = xmlnode_from_str(xml, -1);

	g_assert_nonnull(n);
	return n;
}

static void
test_unwrap_carbon(void)
{
	xmlnode *outer = parse(
		"<message xmlns='jabber:client' from='alice@x' to='alice@x/r'>"
		"<sent xmlns='urn:xmpp:carbons:2'>"
		"<forwarded xmlns='urn:xmpp:forward:0'>"
		"<message xmlns='jabber:client' to='bob@x' from='alice@x/p'>"
		"<body>hi</body></message></forwarded></sent></message>");
	xmlnode *sent = xmlnode_get_child(outer, "sent");
	xmlnode *inner = x2_forward_get_message(sent);

	g_assert_nonnull(inner);
	g_assert_cmpstr(xmlnode_get_attrib(inner, "to"), ==, "bob@x");
	g_assert_nonnull(x2_forward_get_forwarded(sent));
	xmlnode_free(outer);
}

static void
test_unwrap_requires_forward_namespace(void)
{
	xmlnode *outer = parse(
		"<result xmlns='urn:xmpp:mam:2' id='1'>"
		"<forwarded xmlns='urn:other'>"
		"<message xmlns='jabber:client'/></forwarded></result>");

	g_assert_null(x2_forward_get_message(outer));
	g_assert_null(x2_forward_get_forwarded(outer));
	xmlnode_free(outer);
}

static void
test_unwrap_without_message(void)
{
	xmlnode *outer = parse(
		"<result xmlns='urn:xmpp:mam:2' id='1'>"
		"<forwarded xmlns='urn:xmpp:forward:0'>"
		"<delay xmlns='urn:xmpp:delay' stamp='2024-01-02T03:04:05Z'/>"
		"</forwarded></result>");

	g_assert_nonnull(x2_forward_get_forwarded(outer));
	g_assert_null(x2_forward_get_message(outer));
	xmlnode_free(outer);
}

static void
test_delay_xep0203(void)
{
	xmlnode *m = parse(
		"<message xmlns='jabber:client'>"
		"<delay xmlns='urn:xmpp:delay' from='x' "
		"stamp='2024-01-02T03:04:05Z'/></message>");
	time_t ts = 0;
	char *from = NULL;

	g_assert_true(x2_delay_parse(m, &ts, &from));
	g_assert_cmpint(ts, ==, 1704164645);
	g_assert_cmpstr(from, ==, "x");
	g_free(from);
	xmlnode_free(m);
}

static void
test_delay_with_milliseconds_and_offset(void)
{
	xmlnode *m = parse(
		"<message xmlns='jabber:client'>"
		"<delay xmlns='urn:xmpp:delay' "
		"stamp='2024-01-02T04:04:05.123+01:00'/></message>");
	time_t ts = 0;

	g_assert_true(x2_delay_parse(m, &ts, NULL));
	g_assert_cmpint(ts, ==, 1704164645);
	xmlnode_free(m);
}

static void
test_delay_legacy(void)
{
	xmlnode *m = parse(
		"<message xmlns='jabber:client'>"
		"<x xmlns='jabber:x:delay' stamp='20240102T03:04:05'/></message>");
	time_t ts = 0;

	g_assert_true(x2_delay_parse(m, &ts, NULL));
	g_assert_cmpint(ts, ==, 1704164645);
	xmlnode_free(m);
}

static void
test_delay_invalid(void)
{
	xmlnode *m = parse(
		"<message xmlns='jabber:client'>"
		"<delay xmlns='urn:xmpp:delay' stamp='yesterday'/></message>");
	time_t ts = 42;
	char *from = (char *) 1;

	g_assert_false(x2_delay_parse(m, &ts, &from));
	g_assert_cmpint(ts, ==, 42);
	g_assert_null(from);
	xmlnode_free(m);

	m = parse("<message xmlns='jabber:client'><delay xmlns='urn:xmpp:delay'/>"
	          "</message>");
	g_assert_false(x2_delay_parse(m, &ts, NULL));
	xmlnode_free(m);

	m = parse("<message xmlns='jabber:client'><body>hi</body></message>");
	g_assert_false(x2_delay_parse(m, &ts, NULL));
	xmlnode_free(m);
}

/* MAM and carbons: the send time is on <forwarded><delay/>, not inside */
static void
test_delay_forwarded_wins(void)
{
	xmlnode *outer = parse(
		"<message xmlns='jabber:client' from='me@x'>"
		"<result xmlns='urn:xmpp:mam:2' id='r1'>"
		"<forwarded xmlns='urn:xmpp:forward:0'>"
		"<delay xmlns='urn:xmpp:delay' stamp='2024-01-02T03:04:05Z'/>"
		"<message xmlns='jabber:client' from='bob@x/p' to='me@x'>"
		"<delay xmlns='urn:xmpp:delay' stamp='2024-01-01T00:00:00Z'/>"
		"<body>hi</body></message></forwarded></result></message>");
	xmlnode *inner = x2_forward_get_message(xmlnode_get_child(outer, "result"));
	time_t ts = 0;

	g_assert_true(x2_forward_delay_parse(inner, &ts));
	g_assert_cmpint(ts, ==, 1704164645);
	xmlnode_free(outer);
}

static void
test_delay_forwarded_falls_back_to_inner(void)
{
	xmlnode *outer = parse(
		"<message xmlns='jabber:client' from='me@x'>"
		"<sent xmlns='urn:xmpp:carbons:2'>"
		"<forwarded xmlns='urn:xmpp:forward:0'>"
		"<delay xmlns='urn:xmpp:delay' stamp='garbage'/>"
		"<message xmlns='jabber:client' from='me@x/p' to='bob@x'>"
		"<delay xmlns='urn:xmpp:delay' stamp='2024-01-01T00:00:00Z'/>"
		"<body>hi</body></message></forwarded></sent></message>");
	xmlnode *inner = x2_forward_get_message(xmlnode_get_child(outer, "sent"));
	xmlnode *plain = parse("<message xmlns='jabber:client'><body>x</body>"
	                       "</message>");
	time_t ts = 0;

	g_assert_true(x2_forward_delay_parse(inner, &ts));
	g_assert_cmpint(ts, ==, 1704067200);
	/* an unwrapped live message has no <forwarded/> parent */
	g_assert_false(x2_forward_delay_parse(plain, &ts));
	g_assert_false(x2_forward_delay_parse(outer, &ts));
	xmlnode_free(plain);
	xmlnode_free(outer);
}

/* a <delay/> on a parent that is not <forwarded/> does not count */
static void
test_delay_other_parent_ignored(void)
{
	xmlnode *outer = parse(
		"<wrap xmlns='urn:other'>"
		"<delay xmlns='urn:xmpp:delay' stamp='2024-01-02T03:04:05Z'/>"
		"<message xmlns='jabber:client'><body>x</body></message></wrap>");
	time_t ts = 0;

	g_assert_false(x2_forward_delay_parse(xmlnode_get_child(outer, "message"),
	                                      &ts));
	xmlnode_free(outer);
}

static void
test_copy_relocates(void)
{
	xmlnode *outer = parse(
		"<message xmlns='jabber:client' from='me@x' to='me@x/r'>"
		"text<received xmlns='urn:xmpp:carbons:2'>"
		"<forwarded xmlns='urn:xmpp:forward:0'>"
		"<delay xmlns='urn:xmpp:delay' stamp='2024-01-02T03:04:05Z'/>"
		"<message xmlns='jabber:client' from='bob@x/p' to='me@x' id='m1'>"
		"<body>hi</body></message></forwarded></received></message>");
	xmlnode *inner = x2_forward_get_message(
		xmlnode_get_child(outer, "received"));
	xmlnode *inner_copy = NULL;
	xmlnode *copy = x2_forward_copy(outer, inner, &inner_copy);
	char *body;
	time_t ts = 0;

	g_assert_nonnull(copy);
	g_assert_true(copy != outer);
	g_assert_nonnull(inner_copy);
	g_assert_true(inner_copy != inner);
	/* the copy outlives the original */
	xmlnode_free(outer);

	g_assert_cmpstr(xmlnode_get_attrib(inner_copy, "id"), ==, "m1");
	body = xmlnode_get_data(xmlnode_get_child(inner_copy, "body"));
	g_assert_cmpstr(body, ==, "hi");
	g_free(body);
	g_assert_true(x2_forward_delay_parse(inner_copy, &ts));
	g_assert_cmpint(ts, ==, 1704164645);

	/* copying the root relocates to the root */
	{
		xmlnode *root_copy = NULL;
		xmlnode *again = x2_forward_copy(copy, copy, &root_copy);

		g_assert_true(root_copy == again);
		xmlnode_free(again);
	}
	xmlnode_free(copy);
}

static void
test_copy_rejects_foreign_node(void)
{
	xmlnode *a = parse("<message xmlns='jabber:client'><body>a</body></message>");
	xmlnode *b = parse("<message xmlns='jabber:client'><body>b</body></message>");
	xmlnode *found = (xmlnode *) 1;

	g_test_expect_message(G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,
	                      "*not inside outer*");
	g_assert_null(x2_forward_copy(a, xmlnode_get_child(b, "body"), &found));
	g_test_assert_expected_messages();
	g_assert_null(found);
	xmlnode_free(a);
	xmlnode_free(b);
}

int
main(int argc, char **argv)
{
	x2_test_init(&argc, &argv);
	g_test_add_func("/forward/unwrap_carbon", test_unwrap_carbon);
	g_test_add_func("/forward/namespace", test_unwrap_requires_forward_namespace);
	g_test_add_func("/forward/no_message", test_unwrap_without_message);
	g_test_add_func("/forward/delay", test_delay_xep0203);
	g_test_add_func("/forward/delay_ms_offset",
	                test_delay_with_milliseconds_and_offset);
	g_test_add_func("/forward/delay_legacy", test_delay_legacy);
	g_test_add_func("/forward/delay_invalid", test_delay_invalid);
	g_test_add_func("/forward/delay_forwarded_wins", test_delay_forwarded_wins);
	g_test_add_func("/forward/delay_forwarded_fallback",
	                test_delay_forwarded_falls_back_to_inner);
	g_test_add_func("/forward/delay_other_parent",
	                test_delay_other_parent_ignored);
	g_test_add_func("/forward/copy_relocates", test_copy_relocates);
	g_test_add_func("/forward/copy_foreign_node", test_copy_rejects_foreign_node);
	return g_test_run();
}
