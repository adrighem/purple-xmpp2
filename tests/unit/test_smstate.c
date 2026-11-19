/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/* Unit tests of core/smstate.c: XEP-0198 counters, queue, 2^32 wrap. */
#include <glib.h>

#include "smstate.h"

static guint freed;

static void
count_free(gpointer p)
{
	freed++;
	g_free(p);
}

static gpointer
item(guint n)
{
	return g_strdup_printf("s%u", n);
}

static X2SmState *
state_with(guint max, guint32 out, guint32 in, guint sent)
{
	X2SmState *st = x2_sm_state_new(max, count_free);
	guint i;

	x2_sm_state_reset(st, out, in);
	for (i = 1; i <= sent; i++)
		x2_sm_state_sent(st, item(i));
	freed = 0;
	return st;
}

static void
test_ack_trims_exactly(void)
{
	X2SmState *st = state_with(100, 0, 0, 10);
	guint trimmed;
	GList *rest;

	g_assert_cmpuint(x2_sm_state_unacked(st), ==, 10);
	g_assert_true(x2_sm_state_ack(st, 3, &trimmed));
	g_assert_cmpuint(trimmed, ==, 3);
	g_assert_cmpuint(freed, ==, 3);
	g_assert_cmpuint(x2_sm_state_queued(st), ==, 7);
	g_assert_cmpuint(x2_sm_state_unacked(st), ==, 7);

	/* a repeated ack is a no-op */
	g_assert_true(x2_sm_state_ack(st, 3, &trimmed));
	g_assert_cmpuint(trimmed, ==, 0);

	rest = x2_sm_state_take_queue(st);
	g_assert_cmpuint(g_list_length(rest), ==, 7);
	g_assert_cmpstr(rest->data, ==, "s4");
	g_assert_cmpstr(g_list_last(rest)->data, ==, "s10");
	g_list_free_full(rest, g_free);
	g_assert_cmpuint(x2_sm_state_queued(st), ==, 0);
	/* counters survive take_queue */
	g_assert_cmpuint(x2_sm_state_unacked(st), ==, 7);
	x2_sm_state_free(st);
}

static void
test_ack_too_high_rejected(void)
{
	X2SmState *st = state_with(100, 0, 0, 2);
	guint trimmed = 99;

	g_assert_false(x2_sm_state_ack(st, 3, &trimmed));
	g_assert_cmpuint(trimmed, ==, 0);
	g_assert_cmpuint(x2_sm_state_acked(st), ==, 0);
	g_assert_cmpuint(x2_sm_state_queued(st), ==, 2);
	/* h behind the last ack is a huge modular delta: also rejected */
	g_assert_true(x2_sm_state_ack(st, 2, NULL));
	g_assert_false(x2_sm_state_ack(st, 1, NULL));
	x2_sm_state_free(st);
}

static void
test_wraparound(void)
{
	/* outbound starts 3 below 2^32: stanzas get 2^32-2, 2^32-1, 0, 1, 2 */
	X2SmState *st = state_with(100, G_MAXUINT32 - 2, G_MAXUINT32 - 1, 5);
	guint trimmed;

	g_assert_cmpuint(x2_sm_state_outbound(st), ==, 2);
	g_assert_cmpuint(x2_sm_state_unacked(st), ==, 5);
	g_assert_true(x2_sm_state_ack(st, G_MAXUINT32, &trimmed));
	g_assert_cmpuint(trimmed, ==, 2);
	g_assert_true(x2_sm_state_ack(st, 1, &trimmed));
	g_assert_cmpuint(trimmed, ==, 2);
	g_assert_cmpuint(x2_sm_state_unacked(st), ==, 1);
	g_assert_false(x2_sm_state_ack(st, 3, NULL));
	g_assert_true(x2_sm_state_ack(st, 2, &trimmed));
	g_assert_cmpuint(trimmed, ==, 1);
	g_assert_cmpuint(x2_sm_state_queued(st), ==, 0);

	/* inbound h wraps to 0 */
	g_assert_cmpuint(x2_sm_state_received(st), ==, G_MAXUINT32);
	g_assert_cmpuint(x2_sm_state_received(st), ==, 0);
	g_assert_cmpuint(x2_sm_state_inbound(st), ==, 0);
	x2_sm_state_free(st);
}

static void
test_request_batching(void)
{
	X2SmState *st = state_with(100, G_MAXUINT32 - 1, 0, 0);
	guint i, requests = 0;

	g_assert_false(x2_sm_state_need_request(st, 1));
	for (i = 1; i <= 20; i++) {
		x2_sm_state_sent(st, item(i));
		if (x2_sm_state_need_request(st, 5)) {
			x2_sm_state_requested(st);
			requests++;
		}
	}
	g_assert_cmpuint(requests, ==, 4);
	g_assert_false(x2_sm_state_need_request(st, 1));
	x2_sm_state_sent(st, item(21));
	g_assert_true(x2_sm_state_need_request(st, 1));
	g_assert_false(x2_sm_state_need_request(st, 5));
	/* an ack past the last request also covers the unrequested stanza */
	g_assert_true(x2_sm_state_ack(st, x2_sm_state_outbound(st), NULL));
	g_assert_false(x2_sm_state_need_request(st, 1));
	x2_sm_state_free(st);
}

static void
test_bounded_queue_evicts_oldest(void)
{
	X2SmState *st = state_with(3, 0, 0, 5);
	guint trimmed;
	GList *rest;

	g_assert_cmpuint(freed, ==, 0);
	g_assert_cmpuint(x2_sm_state_queued(st), ==, 3);
	g_assert_cmpuint(x2_sm_state_evicted(st), ==, 2);
	g_assert_cmpuint(x2_sm_state_unacked(st), ==, 5);
	/* ack 3: only s3 is still queued among 1..3 */
	g_assert_true(x2_sm_state_ack(st, 3, &trimmed));
	g_assert_cmpuint(trimmed, ==, 1);
	rest = x2_sm_state_take_queue(st);
	g_assert_cmpstr(rest->data, ==, "s4");
	g_list_free_full(rest, g_free);
	x2_sm_state_free(st);
}

static void
test_reset_drops_queue(void)
{
	X2SmState *st = state_with(100, 7, 9, 4);

	g_assert_cmpuint(x2_sm_state_reset(st, 0, 0), ==, 4);
	g_assert_cmpuint(freed, ==, 4);
	g_assert_cmpuint(x2_sm_state_outbound(st), ==, 0);
	g_assert_cmpuint(x2_sm_state_inbound(st), ==, 0);
	g_assert_cmpuint(x2_sm_state_unacked(st), ==, 0);
	g_assert_cmpuint(x2_sm_state_evicted(st), ==, 0);
	x2_sm_state_free(st);
	x2_sm_state_free(NULL);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/smstate/ack-trims-exactly", test_ack_trims_exactly);
	g_test_add_func("/smstate/ack-too-high", test_ack_too_high_rejected);
	g_test_add_func("/smstate/wraparound", test_wraparound);
	g_test_add_func("/smstate/request-batching", test_request_batching);
	g_test_add_func("/smstate/bounded-queue", test_bounded_queue_evicts_oldest);
	g_test_add_func("/smstate/reset", test_reset_drops_queue);
	return g_test_run();
}
