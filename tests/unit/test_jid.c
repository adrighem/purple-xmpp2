/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/* Unit tests of core/jid.c: the bare-JID helpers. */
#include <glib.h>

#include "jid.h"

static void
test_bare_len(void)
{
	g_assert_cmpuint(x2_jid_bare_len("a@x.org"), ==, 7);
	g_assert_cmpuint(x2_jid_bare_len("a@x.org/home"), ==, 7);
	g_assert_cmpuint(x2_jid_bare_len("a@x.org/a/b"), ==, 7);
	g_assert_cmpuint(x2_jid_bare_len("a@x.org/"), ==, 7);
	g_assert_cmpuint(x2_jid_bare_len("/res"), ==, 0);
	g_assert_cmpuint(x2_jid_bare_len(""), ==, 0);
}

static void
test_bare(void)
{
	char *s;

	s = x2_jid_bare("a@x.org/home/x");
	g_assert_cmpstr(s, ==, "a@x.org");
	g_free(s);
	s = x2_jid_bare("a@x.org");
	g_assert_cmpstr(s, ==, "a@x.org");
	g_free(s);
	/* case is kept: this is not a normaliser */
	s = x2_jid_bare("A@X.org/r");
	g_assert_cmpstr(s, ==, "A@X.org");
	g_free(s);
	g_assert_null(x2_jid_bare(NULL));
	g_assert_null(x2_jid_bare(""));
}

static void
test_bare_equal(void)
{
	g_assert_true(x2_jid_bare_equal("a@x.org", "a@x.org"));
	g_assert_true(x2_jid_bare_equal("a@x.org/res/with/slash", "a@x.org"));
	g_assert_false(x2_jid_bare_equal("xa@x.org", "a@x.org"));
	g_assert_false(x2_jid_bare_equal("a@x.org.evil", "a@x.org"));
	g_assert_false(x2_jid_bare_equal("a@x.org", "a@x.org/res"));
	g_assert_false(x2_jid_bare_equal("A@x.org", "a@x.org"));
	g_assert_false(x2_jid_bare_equal(NULL, "a@x.org"));
	g_assert_false(x2_jid_bare_equal("a@x.org", NULL));
}

static void
test_same_bare(void)
{
	g_assert_true(x2_jid_same_bare("a@x.org", "a@x.org"));
	g_assert_true(x2_jid_same_bare("A@X.org/home", "a@x.ORG/work"));
	g_assert_true(x2_jid_same_bare("a@x.org/home", "a@x.org"));
	g_assert_true(x2_jid_same_bare("a@x.org/", "a@x.org"));
	g_assert_false(x2_jid_same_bare("a@x.org", "b@x.org"));
	g_assert_false(x2_jid_same_bare("a@x.org", "a@x.org.evil"));
	g_assert_false(x2_jid_same_bare("a@x.org", "ba@x.org"));
	g_assert_false(x2_jid_same_bare(NULL, "a@x.org"));
	g_assert_false(x2_jid_same_bare("a@x.org", NULL));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/jid/bare_len", test_bare_len);
	g_test_add_func("/jid/bare", test_bare);
	g_test_add_func("/jid/bare_equal", test_bare_equal);
	g_test_add_func("/jid/same_bare", test_same_bare);
	return g_test_run();
}
