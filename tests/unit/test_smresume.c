/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/* Unit tests of core/smresume.c: bounded XEP-0198 resumption attempts. */
#include <glib.h>

#include "smresume.h"

#define S(x) ((gint64)(x) * G_USEC_PER_SEC)

static void
test_not_resumable(void)
{
	X2SmResume r = { 0 };

	g_assert_cmpint(x2_sm_resume_lost(&r, FALSE, 300, 1000, 0), ==, -1);
	g_assert_cmpint(r.phase, ==, X2_SM_RESUME_IDLE);
	/* nothing to start or fail */
	g_assert_false(x2_sm_resume_start(&r, 0));
	g_assert_cmpint(x2_sm_resume_failed(&r, 0), ==, -1);
}

static void
test_resumed_first_attempt(void)
{
	X2SmResume r = { 0 };

	g_assert_cmpint(x2_sm_resume_lost(&r, TRUE, 300, 1000, S(10)), ==, 1000);
	g_assert_cmpint(r.phase, ==, X2_SM_RESUME_WAITING);
	/* a second loss while resuming does not restart the window */
	g_assert_cmpint(x2_sm_resume_lost(&r, TRUE, 300, 1000, S(11)), ==, -1);
	g_assert_true(x2_sm_resume_start(&r, S(11)));
	g_assert_cmpint(r.phase, ==, X2_SM_RESUME_ATTEMPTING);
	g_assert_cmpuint(r.attempts, ==, 1);
	x2_sm_resume_reset(&r);
	g_assert_cmpint(r.phase, ==, X2_SM_RESUME_IDLE);
	/* the next loss is a fresh window */
	g_assert_cmpint(x2_sm_resume_lost(&r, TRUE, 300, 0, S(500)), ==, 0);
	g_assert_cmpint(r.deadline, ==, S(800));
}

static void
test_bounded_attempts(void)
{
	X2SmResume r = { 0 };
	guint i;

	x2_sm_resume_lost(&r, TRUE, 300, 1000, 0);
	for (i = 1; i < X2_SM_RESUME_ATTEMPTS; i++) {
		g_assert_true(x2_sm_resume_start(&r, S(i)));
		g_assert_cmpint(x2_sm_resume_failed(&r, S(i)), ==,
		                X2_SM_RESUME_RETRY_DELAY_MS);
	}
	g_assert_true(x2_sm_resume_start(&r, S(i)));
	/* the last attempt failed: give up */
	g_assert_cmpint(x2_sm_resume_failed(&r, S(i)), ==, -1);
	g_assert_cmpint(r.phase, ==, X2_SM_RESUME_IDLE);
}

static void
test_window_from_max(void)
{
	X2SmResume r = { 0 };

	g_assert_cmpuint(x2_sm_resume_window(0), ==,
	                 X2_SM_RESUME_DEFAULT_WINDOW_S);
	g_assert_cmpuint(x2_sm_resume_window(5), ==, 5);
	g_assert_cmpuint(x2_sm_resume_window(G_MAXUINT), ==,
	                 X2_SM_RESUME_MAX_WINDOW_S);

	/* the server keeps the session 5 s: a retry 5 s later is too late */
	x2_sm_resume_lost(&r, TRUE, 5, 1000, 0);
	g_assert_true(x2_sm_resume_start(&r, S(1)));
	g_assert_cmpint(x2_sm_resume_failed(&r, S(1)), ==, -1);

	/* the first attempt itself is past the window */
	x2_sm_resume_lost(&r, TRUE, 5, 1000, 0);
	g_assert_false(x2_sm_resume_start(&r, S(5)));
	g_assert_cmpint(r.phase, ==, X2_SM_RESUME_IDLE);
}

static void
test_failed_needs_attempt(void)
{
	X2SmResume r = { 0 };

	/* a transport error while waiting (no attempt running) is ignored */
	x2_sm_resume_lost(&r, TRUE, 300, 1000, 0);
	g_assert_cmpint(x2_sm_resume_failed(&r, S(1)), ==, -1);
	g_assert_cmpint(r.phase, ==, X2_SM_RESUME_IDLE);
}

static void
test_reply_ok(void)
{
	/* only after our <resume/> and after SASL */
	g_assert_true(x2_sm_resume_reply_ok(TRUE, TRUE));
	g_assert_false(x2_sm_resume_reply_ok(FALSE, TRUE));
	g_assert_false(x2_sm_resume_reply_ok(TRUE, FALSE));
	g_assert_false(x2_sm_resume_reply_ok(FALSE, FALSE));
}

static void
test_id_log(void)
{
	char *s;

	s = x2_sm_id_log("0f3a9c1e-72b4-4d55-8e21-aa00bb11cc22");
	g_assert_cmpstr(s, ==, "0f3a...");
	g_free(s);
	s = x2_sm_id_log("abcd");
	g_assert_cmpstr(s, ==, "...");
	g_free(s);
	s = x2_sm_id_log("abcde");
	g_assert_cmpstr(s, ==, "abcd...");
	g_free(s);
	s = x2_sm_id_log("");
	g_assert_cmpstr(s, ==, "...");
	g_free(s);
	s = x2_sm_id_log(NULL);
	g_assert_cmpstr(s, ==, "(none)");
	g_free(s);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/smresume/not-resumable", test_not_resumable);
	g_test_add_func("/smresume/resumed-first", test_resumed_first_attempt);
	g_test_add_func("/smresume/bounded-attempts", test_bounded_attempts);
	g_test_add_func("/smresume/window", test_window_from_max);
	g_test_add_func("/smresume/failed-needs-attempt",
	                test_failed_needs_attempt);
	g_test_add_func("/smresume/reply-ok", test_reply_ok);
	g_test_add_func("/smresume/id-log", test_id_log);
	return g_test_run();
}
