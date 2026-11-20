/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/* XEP-0198 counters and unacked queue, see smstate.h. */
#include "smstate.h"

typedef struct {
	guint32 seq;
	gpointer item;
} SmEntry;

struct _X2SmState {
	guint32 out;		/* outbound stanzas counted */
	guint32 acked;		/* last h from the server */
	guint32 requested;	/* out at the time of the last <r/> */
	guint32 in;		/* inbound stanzas handled (our h) */
	guint evicted;
	guint max_queue;
	GQueue queue;		/* of SmEntry, oldest first */
	GDestroyNotify free_item;
};

static void
entry_free(X2SmState *st, SmEntry *e)
{
	if (st->free_item != NULL && e->item != NULL)
		st->free_item(e->item);
	g_free(e);
}

X2SmState *
x2_sm_state_new(guint max_queue, GDestroyNotify free_item)
{
	X2SmState *st;

	g_return_val_if_fail(max_queue > 0, NULL);
	st = g_new0(X2SmState, 1);
	st->max_queue = max_queue;
	st->free_item = free_item;
	g_queue_init(&st->queue);
	return st;
}

void
x2_sm_state_free(X2SmState *st)
{
	if (st == NULL)
		return;
	x2_sm_state_reset(st, 0, 0);
	g_free(st);
}

guint
x2_sm_state_reset(X2SmState *st, guint32 out, guint32 in)
{
	guint dropped = 0;
	SmEntry *e;

	g_return_val_if_fail(st != NULL, 0);
	while ((e = g_queue_pop_head(&st->queue)) != NULL) {
		entry_free(st, e);
		dropped++;
	}
	st->out = out;
	st->acked = out;
	st->requested = out;
	st->in = in;
	st->evicted = 0;
	return dropped;
}

guint32
x2_sm_state_sent(X2SmState *st, gpointer item)
{
	SmEntry *e;

	g_return_val_if_fail(st != NULL, 0);
	if (g_queue_get_length(&st->queue) >= st->max_queue) {
		entry_free(st, g_queue_pop_head(&st->queue));
		st->evicted++;
	}
	e = g_new(SmEntry, 1);
	e->seq = ++st->out;
	e->item = item;
	g_queue_push_tail(&st->queue, e);
	return e->seq;
}

guint32
x2_sm_state_received(X2SmState *st)
{
	g_return_val_if_fail(st != NULL, 0);
	return ++st->in;
}

guint32
x2_sm_state_inbound(const X2SmState *st)
{
	return st->in;
}

guint32
x2_sm_state_outbound(const X2SmState *st)
{
	return st->out;
}

guint32
x2_sm_state_acked(const X2SmState *st)
{
	return st->acked;
}

guint32
x2_sm_state_unacked(const X2SmState *st)
{
	return st->out - st->acked;
}

guint
x2_sm_state_queued(const X2SmState *st)
{
	return g_queue_get_length((GQueue *)&st->queue);
}

guint
x2_sm_state_evicted(const X2SmState *st)
{
	return st->evicted;
}

gboolean
x2_sm_state_ack(X2SmState *st, guint32 h, guint *trimmed)
{
	guint32 delta;
	guint n = 0;
	SmEntry *e;

	if (trimmed != NULL)
		*trimmed = 0;
	g_return_val_if_fail(st != NULL, FALSE);
	/* All arithmetic is modulo 2^32 (XEP-0198 section 4). */
	delta = h - st->acked;
	if (delta > st->out - st->acked)
		return FALSE;
	while ((e = g_queue_peek_head(&st->queue)) != NULL
	       && (guint32)(e->seq - st->acked) <= delta) {
		g_queue_pop_head(&st->queue);
		entry_free(st, e);
		n++;
	}
	st->acked = h;
	/* An ack covering stanzas sent after the last <r/> covers them too. */
	if ((guint32)(st->requested - st->acked) > st->out - st->acked)
		st->requested = st->acked;
	if (trimmed != NULL)
		*trimmed = n;
	return TRUE;
}

gboolean
x2_sm_state_need_request(const X2SmState *st, guint threshold)
{
	g_return_val_if_fail(st != NULL, FALSE);
	return st->out - st->requested >= MAX(threshold, 1u);
}

void
x2_sm_state_requested(X2SmState *st)
{
	g_return_if_fail(st != NULL);
	st->requested = st->out;
}

X2SmResend
x2_sm_state_resumed(X2SmState *st, guint32 h, guint *trimmed, GList **resend)
{
	GList *items = NULL, *l;

	*resend = NULL;
	g_return_val_if_fail(st != NULL, X2_SM_RESEND_INCOMPLETE);
	if (!x2_sm_state_ack(st, h, trimmed))
		return X2_SM_RESEND_H_TOO_HIGH;
	/* Every unacked stanza must still be queued, with a copy. */
	if (g_queue_get_length(&st->queue) != x2_sm_state_unacked(st))
		return X2_SM_RESEND_INCOMPLETE;
	for (l = st->queue.head; l != NULL; l = l->next) {
		SmEntry *e = l->data;

		if (e->item == NULL) {
			g_list_free(items);
			return X2_SM_RESEND_INCOMPLETE;
		}
		items = g_list_prepend(items, e->item);
	}
	*resend = g_list_reverse(items);
	return X2_SM_RESEND_OK;
}

GList *
x2_sm_state_take_queue(X2SmState *st)
{
	GList *items = NULL;
	SmEntry *e;

	g_return_val_if_fail(st != NULL, NULL);
	while ((e = g_queue_pop_tail(&st->queue)) != NULL) {
		items = g_list_prepend(items, e->item);
		g_free(e);
	}
	return items;
}
