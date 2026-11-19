/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/* Unit tests of core/sid.c: origin-id, trusted stanza-id. */
#include <string.h>
#include <glib.h>

#include "x2test.h"

#include "sid.h"

#define MSG_OPEN "<message xmlns='jabber:client' from='bob@x/r' type='chat'>"

static xmlnode *
parse(const char *xml)
{
	xmlnode *n = xmlnode_from_str(xml, -1);

	g_assert_nonnull(n);
	return n;
}

static void
test_origin_id(void)
{
	xmlnode *m = parse(MSG_OPEN "<body>hi</body>"
		"<origin-id xmlns='urn:xmpp:sid:0' id='abc'/></message>");
	char *id = x2_sid_origin_id(m);

	g_assert_cmpstr(id, ==, "abc");
	g_free(id);
	xmlnode_free(m);
}

static void
test_origin_id_absent_or_wrong_ns(void)
{
	xmlnode *m = parse(MSG_OPEN "<origin-id xmlns='urn:other' id='abc'/>"
		"<origin-id xmlns='urn:xmpp:sid:0'/></message>");

	g_assert_null(x2_sid_origin_id(m));
	xmlnode_free(m);

	m = parse(MSG_OPEN "<body>hi</body></message>");
	g_assert_null(x2_sid_origin_id(m));
	xmlnode_free(m);
}

static void
test_stanza_id_requires_matching_by(void)
{
	xmlnode *m = parse(MSG_OPEN
		"<stanza-id xmlns='urn:xmpp:sid:0' id='forged' by='evil@x'/>"
		"<stanza-id xmlns='urn:xmpp:sid:0' id='real' by='alice@x'/>"
		"</message>");
	char *id = x2_sid_stanza_id(m, "alice@x");

	/* the forged one comes first and must be skipped */
	g_assert_cmpstr(id, ==, "real");
	g_free(id);

	g_assert_null(x2_sid_stanza_id(m, "carol@x"));
	/* "by" is compared exactly, a resource or case change is not a match */
	g_assert_null(x2_sid_stanza_id(m, "alice@x/res"));
	g_assert_null(x2_sid_stanza_id(m, "ALICE@x"));
	xmlnode_free(m);
}

static void
test_stanza_id_missing_by_or_id(void)
{
	xmlnode *m = parse(MSG_OPEN
		"<stanza-id xmlns='urn:xmpp:sid:0' id='noby'/>"
		"<stanza-id xmlns='urn:xmpp:sid:0' by='alice@x'/>"
		"<stanza-id xmlns='urn:xmpp:sid:0' id='' by='alice@x'/></message>");

	g_assert_null(x2_sid_stanza_id(m, "alice@x"));
	xmlnode_free(m);
}

static void
test_stanza_id_wrong_namespace(void)
{
	xmlnode *m = parse(MSG_OPEN
		"<stanza-id xmlns='urn:other' id='x' by='alice@x'/></message>");

	g_assert_null(x2_sid_stanza_id(m, "alice@x"));
	xmlnode_free(m);
}

static void
test_add_origin_id_roundtrip(void)
{
	xmlnode *m = xmlnode_new("message");
	char *id = x2_sid_new_id();
	char *back;

	g_assert_cmpuint(strlen(id), ==, 36);
	x2_sid_add_origin_id(m, id);
	back = x2_sid_origin_id(m);
	g_assert_cmpstr(back, ==, id);
	g_free(back);

	xmlnode_free(m);
	g_free(id);
}

static void
test_new_ids_differ(void)
{
	char *a = x2_sid_new_id();
	char *b = x2_sid_new_id();

	g_assert_cmpstr(a, !=, b);
	g_free(a);
	g_free(b);
}

int
main(int argc, char **argv)
{
	x2_test_init(&argc, &argv);
	g_test_add_func("/sid/origin_id", test_origin_id);
	g_test_add_func("/sid/origin_id_absent", test_origin_id_absent_or_wrong_ns);
	g_test_add_func("/sid/stanza_id_by", test_stanza_id_requires_matching_by);
	g_test_add_func("/sid/stanza_id_incomplete",
	                test_stanza_id_missing_by_or_id);
	g_test_add_func("/sid/stanza_id_namespace", test_stanza_id_wrong_namespace);
	g_test_add_func("/sid/add_origin_id", test_add_origin_id_roundtrip);
	g_test_add_func("/sid/new_ids_differ", test_new_ids_differ);
	return g_test_run();
}
