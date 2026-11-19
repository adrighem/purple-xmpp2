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
	return g_test_run();
}
