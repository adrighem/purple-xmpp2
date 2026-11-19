/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/*
 * Unit tests of the room parts of core/mam_proto.c: the room-archive result
 * check (ARCHITECTURE 8, PLAN M4 item 10) and the choice of the first room
 * query (ARCHITECTURE 7.2).
 */
#include <glib.h>

#include "x2test.h"

#include "mam_proto.h"

#define ROOM "test@conference.example.org"
#define QID "q1"
#define DAY (24 * 3600)

#define RESULT(outer_from, qid, type, inner_from) \
	"<message " outer_from " to='alice@example.org/pc'>" \
	"<result xmlns='urn:xmpp:mam:2' queryid='" qid "' id='r1'>" \
	"<forwarded xmlns='urn:xmpp:forward:0'>" \
	"<delay xmlns='urn:xmpp:delay' stamp='2026-01-01T00:00:00Z'/>" \
	"<message xmlns='jabber:client' type='" type "' from='" inner_from \
	"' to='alice@example.org'><body>hi</body></message>" \
	"</forwarded></result></message>"

static X2MamCheck
check(const char *xml, const char *qid, xmlnode **inner, const char **why)
{
	static xmlnode *keep;
	const char *id;
	X2MamCheck c;

	if (keep != NULL)
		xmlnode_free(keep);
	keep = xmlnode_from_str(xml, -1);
	g_assert_nonnull(keep);
	c = x2_mam_room_result_check(keep, ROOM, qid, inner, &id, why);
	if (c == X2_MAM_VALID)
		g_assert_cmpstr(id, ==, "r1");
	return c;
}

static void
test_room_result_valid(void)
{
	xmlnode *inner;
	const char *why;

	g_assert_cmpint(check(RESULT("from='" ROOM "'", QID, "groupchat",
	                             ROOM "/carol"), QID, &inner, &why),
	                ==, X2_MAM_VALID);
	g_assert_cmpstr(xmlnode_get_attrib(inner, "from"), ==, ROOM "/carol");
	g_assert_cmpstr(xmlnode_get_attrib(inner, "type"), ==, "groupchat");
}

/* item 10: a room result with inner type='chat' is still a room message */
static void
test_room_result_chat_is_groupchat(void)
{
	xmlnode *inner;
	const char *why;

	g_assert_cmpint(check(RESULT("from='" ROOM "'", QID, "chat",
	                             ROOM "/carol"), QID, &inner, &why),
	                ==, X2_MAM_VALID);
	g_assert_cmpstr(xmlnode_get_attrib(inner, "type"), ==, "groupchat");
}

/* item 10: inner from is not the room */
static void
test_room_result_forged_inner(void)
{
	xmlnode *inner;
	const char *why;

	g_assert_cmpint(check(RESULT("from='" ROOM "'", QID, "groupchat",
	                             "mallory@example.org/x"), QID, &inner, &why),
	                ==, X2_MAM_FORGED);
	g_assert_cmpstr(why, ==, "inner message is not from the room");
	g_assert_null(inner);
	/* another room on the same service */
	g_assert_cmpint(check(RESULT("from='" ROOM "'", QID, "groupchat",
	                             "other@conference.example.org/carol"), QID,
	                      &inner, &why), ==, X2_MAM_FORGED);
	/* a JID that merely starts like the room */
	g_assert_cmpint(check(RESULT("from='" ROOM "'", QID, "groupchat",
	                             ROOM ".evil/carol"), QID, &inner, &why),
	                ==, X2_MAM_FORGED);
}

static void
test_room_result_forged_outer(void)
{
	xmlnode *inner;
	const char *why;

	/* an occupant relaying a hand-made result through the room */
	g_assert_cmpint(check(RESULT("from='" ROOM "/carol'", QID, "groupchat",
	                             ROOM "/carol"), QID, &inner, &why),
	                ==, X2_MAM_FORGED);
	g_assert_cmpstr(why, ==, "outer from is not the room");
	/* absent from is our own server: never the room */
	g_assert_cmpint(check(RESULT("", QID, "groupchat", ROOM "/carol"), QID,
	                      &inner, &why), ==, X2_MAM_FORGED);
	/* a contact */
	g_assert_cmpint(check(RESULT("from='bob@example.org/x'", QID,
	                             "groupchat", ROOM "/carol"), QID,
	                      &inner, &why), ==, X2_MAM_FORGED);
}

static void
test_room_result_forged_queryid(void)
{
	xmlnode *inner;
	const char *why;

	g_assert_cmpint(check(RESULT("from='" ROOM "'", "random", "groupchat",
	                             ROOM "/carol"), QID, &inner, &why),
	                ==, X2_MAM_FORGED);
	g_assert_cmpint(check(RESULT("from='" ROOM "'", QID, "groupchat",
	                             ROOM "/carol"), NULL, &inner, &why),
	                ==, X2_MAM_FORGED);
}

static void
test_room_not_a_result(void)
{
	xmlnode *inner;
	const char *why;

	g_assert_cmpint(check("<message from='" ROOM "/carol' type='groupchat'>"
	                      "<body>x</body></message>", QID, &inner, &why),
	                ==, X2_MAM_NOT_RESULT);
}

static void
test_room_start(void)
{
	time_t now = 1800000000;

	/* first join: last page */
	g_assert_cmpint(x2_mam_room_start(NULL, 0, now, 2), ==,
	                X2_MAM_ROOM_LAST_PAGE);
	/* fresh cursor */
	g_assert_cmpint(x2_mam_room_start("c1", now - DAY, now, 2), ==,
	                X2_MAM_ROOM_AFTER_CURSOR);
	g_assert_cmpint(x2_mam_room_start("c1", now - 2 * DAY, now, 2), ==,
	                X2_MAM_ROOM_AFTER_CURSOR);
	/* older than the threshold */
	g_assert_cmpint(x2_mam_room_start("c1", now - 2 * DAY - 1, now, 2), ==,
	                X2_MAM_ROOM_LAST_PAGE);
	/* cursor without a timestamp, empty cursor, threshold 0 */
	g_assert_cmpint(x2_mam_room_start("c1", 0, now, 2), ==,
	                X2_MAM_ROOM_LAST_PAGE);
	g_assert_cmpint(x2_mam_room_start("", now, now, 2), ==,
	                X2_MAM_ROOM_LAST_PAGE);
	g_assert_cmpint(x2_mam_room_start("c1", now, now, 0), ==,
	                X2_MAM_ROOM_LAST_PAGE);
}

int
main(int argc, char **argv)
{
	x2_test_init(&argc, &argv);

	g_test_add_func("/mam-muc/result/valid", test_room_result_valid);
	g_test_add_func("/mam-muc/result/chat-is-groupchat",
	                test_room_result_chat_is_groupchat);
	g_test_add_func("/mam-muc/result/forged-inner",
	                test_room_result_forged_inner);
	g_test_add_func("/mam-muc/result/forged-outer",
	                test_room_result_forged_outer);
	g_test_add_func("/mam-muc/result/forged-queryid",
	                test_room_result_forged_queryid);
	g_test_add_func("/mam-muc/result/not-result", test_room_not_a_result);
	g_test_add_func("/mam-muc/start", test_room_start);
	return g_test_run();
}
