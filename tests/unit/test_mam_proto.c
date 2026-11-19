/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/*
 * Unit tests of core/mam_proto.c: query building, <fin/> parsing and the
 * checks that keep forged archive results out (ARCHITECTURE 8).
 */
#include <string.h>
#include <glib.h>

#include "x2test.h"

#include "mam_proto.h"

#define ME "alice@example.org"
#define QID "q1"

static xmlnode *
parse(const char *xml)
{
	xmlnode *n = xmlnode_from_str(xml, -1);

	g_assert_nonnull(n);
	return n;
}

static gboolean
contains(xmlnode *n, const char *needle)
{
	char *s = xmlnode_to_str(n, NULL);
	gboolean found = strstr(s, needle) != NULL;

	if (!found)
		g_test_message("%s not in %s", needle, s);
	g_free(s);
	return found;
}

/* ---- queries ---- */

static void
test_query_after(void)
{
	X2MamQuery q = { .queryid = QID, .after = "c1", .max = 100 };
	xmlnode *query = x2_mam_query_new(&q);

	g_assert_nonnull(query);
	g_assert_cmpstr(xmlnode_get_namespace(query), ==, X2_NS_MAM);
	g_assert_cmpstr(xmlnode_get_attrib(query, "queryid"), ==, QID);
	g_assert_true(contains(query, "<max>100</max>"));
	g_assert_true(contains(query, "<after>c1</after>"));
	g_assert_false(contains(query, "var='start'"));
	g_assert_false(contains(query, "<before"));
	g_assert_true(contains(query, "<value>urn:xmpp:mam:2</value>"));
	xmlnode_free(query);
}

static void
test_query_start(void)
{
	X2MamQuery q = { .queryid = QID, .start = 86400 + 3661, .max = 100 };
	xmlnode *query = x2_mam_query_new(&q);

	g_assert_true(contains(query, "var='start'"));
	g_assert_true(contains(query, "<value>1970-01-02T01:01:01Z</value>"));
	g_assert_false(contains(query, "<after"));
	xmlnode_free(query);
}

static void
test_query_last_page_with(void)
{
	X2MamQuery q = { .queryid = QID, .with = "bob@example.org",
	                 .last_page = TRUE, .max = 1 };
	xmlnode *query = x2_mam_query_new(&q);

	g_assert_true(contains(query, "<before/>"));
	g_assert_true(contains(query, "<max>1</max>"));
	g_assert_true(contains(query, "<value>bob@example.org</value>"));
	xmlnode_free(query);
}

static void
test_query_ids_probe(void)
{
	X2MamQuery q = { .queryid = QID, .id = "c1", .max = 1 };
	xmlnode *query = x2_mam_query_new(&q);

	g_assert_true(contains(query, "var='ids'"));
	g_assert_true(contains(query, "<value>c1</value>"));
	g_assert_false(contains(query, "<after"));
	g_assert_false(contains(query, "<before"));
	xmlnode_free(query);
}

static void
test_query_rejects_after_and_before(void)
{
	X2MamQuery q = { .queryid = QID, .after = "x", .last_page = TRUE };

	g_test_expect_message(G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL, "*after*");
	g_assert_null(x2_mam_query_new(&q));
	g_test_assert_expected_messages();
}

/* ---- <fin/> ---- */

static void
test_fin_complete(void)
{
	xmlnode *iq = parse(
		"<iq type='result' id='1'><fin xmlns='urn:xmpp:mam:2' "
		"complete='true'><set xmlns='http://jabber.org/protocol/rsm'>"
		"<first index='0'>a</first><last>z</last></set></fin></iq>");
	X2MamFin fin;

	g_assert_true(x2_mam_fin_parse(iq, &fin));
	g_assert_true(fin.complete);
	g_assert_cmpstr(fin.first, ==, "a");
	g_assert_cmpstr(fin.last, ==, "z");
	x2_mam_fin_clear(&fin);
	g_assert_null(fin.last);
	xmlnode_free(iq);
}

/* Review fix 10: xs:boolean also allows "1" (and "0", "false") */
static void
test_fin_complete_boolean(void)
{
	static const struct { const char *attr; gboolean complete; } cases[] = {
		{ "complete='1'", TRUE }, { "complete='true'", TRUE },
		{ "complete='0'", FALSE }, { "complete='false'", FALSE },
		{ "", FALSE },
	};
	gsize i;

	for (i = 0; i < G_N_ELEMENTS(cases); i++) {
		char *xml = g_strdup_printf("<fin xmlns='urn:xmpp:mam:2' %s/>",
		                            cases[i].attr);
		xmlnode *n = parse(xml);
		X2MamFin fin;

		g_assert_true(x2_mam_fin_parse(n, &fin));
		g_assert_cmpint(fin.complete, ==, cases[i].complete);
		x2_mam_fin_clear(&fin);
		xmlnode_free(n);
		g_free(xml);
	}
}

static void
test_fin_incomplete_without_last(void)
{
	xmlnode *iq = parse(
		"<iq type='result' id='1'><fin xmlns='urn:xmpp:mam:2'>"
		"<set xmlns='http://jabber.org/protocol/rsm'><count>0</count>"
		"</set></fin></iq>");
	X2MamFin fin;

	g_assert_true(x2_mam_fin_parse(iq, &fin));
	g_assert_false(fin.complete);
	g_assert_null(fin.first);
	g_assert_null(fin.last);
	x2_mam_fin_clear(&fin);
	xmlnode_free(iq);
}

static void
test_fin_missing(void)
{
	xmlnode *iq = parse("<iq type='result' id='1'><fin xmlns='urn:x'/></iq>");
	X2MamFin fin;

	g_assert_false(x2_mam_fin_parse(iq, &fin));
	g_assert_null(fin.last);
	xmlnode_free(iq);
}

static void
test_item_not_found(void)
{
	xmlnode *e = parse(
		"<iq type='error' id='1'><error type='cancel'><item-not-found "
		"xmlns='urn:ietf:params:xml:ns:xmpp-stanzas'/></error></iq>");
	xmlnode *other = parse(
		"<iq type='error' id='1'><error type='cancel'><bad-request "
		"xmlns='urn:ietf:params:xml:ns:xmpp-stanzas'/></error></iq>");

	g_assert_true(x2_mam_error_is_item_not_found(e));
	g_assert_false(x2_mam_error_is_item_not_found(other));
	xmlnode_free(e);
	xmlnode_free(other);
}

/* ---- results: the security checks ---- */

static X2MamCheck
check(const char *xml, xmlnode **inner, const char **id, const char **why)
{
	static xmlnode *keep;

	if (keep != NULL)
		xmlnode_free(keep);
	keep = parse(xml);
	return x2_mam_result_check(keep, ME, QID, inner, id, why);
}

#define RESULT(outer_from, qid, inner_from, inner_to) \
	"<message " outer_from " to='" ME "/pc'>" \
	"<result xmlns='urn:xmpp:mam:2' queryid='" qid "' id='r1'>" \
	"<forwarded xmlns='urn:xmpp:forward:0'>" \
	"<delay xmlns='urn:xmpp:delay' stamp='2026-01-01T00:00:00Z'/>" \
	"<message xmlns='jabber:client' type='chat' from='" inner_from "' " \
	"to='" inner_to "'><body>hi</body></message>" \
	"</forwarded></result></message>"

static void
test_result_valid(void)
{
	xmlnode *inner;
	const char *id, *why;

	g_assert_cmpint(check(RESULT("from='" ME "'", QID, "bob@x/r", ME "/pc"),
	                      &inner, &id, &why), ==, X2_MAM_VALID);
	g_assert_cmpstr(id, ==, "r1");
	g_assert_cmpstr(xmlnode_get_attrib(inner, "from"), ==, "bob@x/r");

	/* absent outer from: the server on our behalf */
	g_assert_cmpint(check(RESULT("", QID, ME "/phone", "carol@x"),
	                      &inner, &id, &why), ==, X2_MAM_VALID);
}

static void
test_result_not_a_result(void)
{
	xmlnode *inner;
	const char *id, *why;

	g_assert_cmpint(check("<message from='bob@x'><body>x</body></message>",
	                      &inner, &id, &why), ==, X2_MAM_NOT_RESULT);
	g_assert_null(inner);
}

static void
test_result_forged_outer_from(void)
{
	xmlnode *inner;
	const char *id, *why;

	/* a contact sending a hand-made result */
	g_assert_cmpint(check(RESULT("from='bob@x/r'", QID, "bob@x", ME),
	                      &inner, &id, &why), ==, X2_MAM_FORGED);
	g_assert_nonnull(why);
	g_assert_null(inner);
	/* our own full JID is another device, not the archive */
	g_assert_cmpint(check(RESULT("from='" ME "/phone'", QID, "bob@x", ME),
	                      &inner, &id, &why), ==, X2_MAM_FORGED);
	/* a domain that merely starts like ours */
	g_assert_cmpint(check(RESULT("from='" ME ".evil'", QID, "bob@x", ME),
	                      &inner, &id, &why), ==, X2_MAM_FORGED);
}

static void
test_result_forged_queryid(void)
{
	xmlnode *inner, *n;
	const char *id, *why;

	g_assert_cmpint(check(RESULT("from='" ME "'", "random", "bob@x", ME),
	                      &inner, &id, &why), ==, X2_MAM_FORGED);
	/* no active query: every result is forged */
	n = parse(RESULT("from='" ME "'", QID, "bob@x", ME));
	g_assert_cmpint(x2_mam_result_check(n, ME, NULL, &inner, &id, &why),
	                ==, X2_MAM_FORGED);
	xmlnode_free(n);
}

static void
test_result_forged_inner(void)
{
	xmlnode *inner;
	const char *id, *why;

	/* account archive result whose inner from and to are both foreign */
	g_assert_cmpint(check(RESULT("from='" ME "'", QID, "bob@x/r",
	                             "carol@x/r"), &inner, &id, &why),
	                ==, X2_MAM_FORGED);
	g_assert_cmpstr(why, ==, "inner message is not to or from us");
	/* a prefix of our JID is not our JID */
	g_assert_cmpint(check(RESULT("from='" ME "'", QID, "bob@x",
	                             "alice@example.or"), &inner, &id, &why),
	                ==, X2_MAM_FORGED);
}

static void
test_result_missing_parts(void)
{
	xmlnode *inner;
	const char *id, *why;

	g_assert_cmpint(check(
		"<message from='" ME "'><result xmlns='urn:xmpp:mam:2' "
		"queryid='" QID "'><forwarded xmlns='urn:xmpp:forward:0'>"
		"<message from='bob@x' to='" ME "'/></forwarded></result></message>",
		&inner, &id, &why), ==, X2_MAM_FORGED);
	g_assert_cmpint(check(
		"<message from='" ME "'><result xmlns='urn:xmpp:mam:2' "
		"queryid='" QID "' id='r1'/></message>",
		&inner, &id, &why), ==, X2_MAM_FORGED);
}

/* Review fix 5: an offline copy is dropped only after a page-cap skip */
static void
test_queued_fate(void)
{
	/* seen: a duplicate, whatever else */
	g_assert_cmpint(x2_mam_queued_fate(TRUE, TRUE, FALSE, TRUE, TRUE), ==,
	                X2_MAM_QUEUED_DUPLICATE);
	/* complete forward catch-up: an unseen offline copy is news */
	g_assert_cmpint(x2_mam_queued_fate(FALSE, TRUE, FALSE, TRUE, TRUE), ==,
	                X2_MAM_QUEUED_DELIVER);
	/* page cap, newest page shown: the copy lies in the skipped part */
	g_assert_cmpint(x2_mam_queued_fate(FALSE, TRUE, TRUE, TRUE, TRUE), ==,
	                X2_MAM_QUEUED_SKIPPED);
	/* incomplete, live (not delayed), or untrusted: shown */
	g_assert_cmpint(x2_mam_queued_fate(FALSE, FALSE, TRUE, TRUE, TRUE), ==,
	                X2_MAM_QUEUED_DELIVER);
	g_assert_cmpint(x2_mam_queued_fate(FALSE, TRUE, TRUE, FALSE, TRUE), ==,
	                X2_MAM_QUEUED_DELIVER);
	g_assert_cmpint(x2_mam_queued_fate(FALSE, TRUE, TRUE, TRUE, FALSE), ==,
	                X2_MAM_QUEUED_DELIVER);
}

static void
test_bare_equal(void)
{
	g_assert_true(x2_mam_bare_equal(ME, ME));
	g_assert_true(x2_mam_bare_equal(ME "/res/with/slash", ME));
	g_assert_false(x2_mam_bare_equal("x" ME, ME));
	g_assert_false(x2_mam_bare_equal(NULL, ME));
}

int
main(int argc, char **argv)
{
	x2_test_init(&argc, &argv);

	g_test_add_func("/mam/query/after", test_query_after);
	g_test_add_func("/mam/query/start", test_query_start);
	g_test_add_func("/mam/query/last-page-with", test_query_last_page_with);
	g_test_add_func("/mam/query/ids-probe", test_query_ids_probe);
	g_test_add_func("/mam/query/after-and-before",
	                test_query_rejects_after_and_before);
	g_test_add_func("/mam/fin/complete", test_fin_complete);
	g_test_add_func("/mam/fin/complete-boolean", test_fin_complete_boolean);
	g_test_add_func("/mam/fin/no-last", test_fin_incomplete_without_last);
	g_test_add_func("/mam/fin/missing", test_fin_missing);
	g_test_add_func("/mam/error/item-not-found", test_item_not_found);
	g_test_add_func("/mam/result/valid", test_result_valid);
	g_test_add_func("/mam/result/not-result", test_result_not_a_result);
	g_test_add_func("/mam/result/forged-outer-from",
	                test_result_forged_outer_from);
	g_test_add_func("/mam/result/forged-queryid", test_result_forged_queryid);
	g_test_add_func("/mam/result/forged-inner", test_result_forged_inner);
	g_test_add_func("/mam/result/missing-parts", test_result_missing_parts);
	g_test_add_func("/mam/bare-equal", test_bare_equal);
	g_test_add_func("/mam/queued-fate", test_queued_fate);
	return g_test_run();
}
