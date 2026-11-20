/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file smstate.h XEP-0198 counters and unacked queue (pure logic)
 *
 * One X2SmState per stream management session.  Counters are the 32-bit
 * sequence numbers of XEP-0198 and wrap modulo 2^32.  Every outbound stanza
 * gets the next sequence number and is queued (oldest first) until an ack
 * <a h='N'/> covers it.  The queue is bounded: past @a max_queue the oldest
 * item is evicted and counted in x2_sm_state_evicted().
 *
 * Items are opaque (the glue stores xmlnode copies); the state owns them
 * and frees them with the GDestroyNotify given at creation, except for
 * x2_sm_state_take_queue(), which hands them to the caller.
 */
#ifndef XMPP2_CORE_SMSTATE_H_
#define XMPP2_CORE_SMSTATE_H_

#include <glib.h>

typedef struct _X2SmState X2SmState;

/** Creates a state with all counters at 0 (max_queue must be > 0). */
X2SmState *x2_sm_state_new(guint max_queue, GDestroyNotify free_item);

/** Frees the state and every queued item. NULL is allowed. */
void x2_sm_state_free(X2SmState *st);

/**
 * Starts a new session: frees the queue and sets the outbound counter to
 * @a out and the inbound counter to @a in (0 and 0 for a fresh <enable/>;
 * other values are for tests of the wraparound).
 *
 * @return The number of queued items that were dropped.
 */
guint x2_sm_state_reset(X2SmState *st, guint32 out, guint32 in);

/**
 * Counts an outbound stanza and queues @a item for it (the state takes
 * ownership).  Evicts the oldest item if the queue is full.
 *
 * @return The sequence number of the stanza.
 */
guint32 x2_sm_state_sent(X2SmState *st, gpointer item);

/** Counts an inbound stanza. @return The new inbound count (our h). */
guint32 x2_sm_state_received(X2SmState *st);

/** Inbound count, the h we report in <a/>. */
guint32 x2_sm_state_inbound(const X2SmState *st);

/** Number of outbound stanzas counted (modulo 2^32). */
guint32 x2_sm_state_outbound(const X2SmState *st);

/** Last h acknowledged by the server. */
guint32 x2_sm_state_acked(const X2SmState *st);

/** Outbound stanzas not yet acknowledged (outbound - acked, modular). */
guint32 x2_sm_state_unacked(const X2SmState *st);

/** Number of items in the queue. */
guint x2_sm_state_queued(const X2SmState *st);

/** Unacked stanzas that were evicted from the full queue this session. */
guint x2_sm_state_evicted(const X2SmState *st);

/**
 * Applies an ack <a h='@a h'/>.  Dequeues and frees every item with a
 * sequence number in (acked, h].
 *
 * @param trimmed  Set to the number of items removed (may be NULL).
 * @return FALSE, with the state unchanged, if @a h acknowledges more
 *         stanzas than were sent (XEP-0198 handled-count-too-high).
 */
gboolean x2_sm_state_ack(X2SmState *st, guint32 h, guint *trimmed);

/**
 * TRUE if at least @a threshold stanzas were sent since the last ack
 * request (x2_sm_state_requested()).  A threshold of 1 means "any".
 */
gboolean x2_sm_state_need_request(const X2SmState *st, guint threshold);

/** Records that an ack request <r/> covering every sent stanza went out. */
void x2_sm_state_requested(X2SmState *st);

typedef enum {
	X2_SM_RESEND_OK = 0,
	X2_SM_RESEND_H_TOO_HIGH,	/* h acknowledges more than was sent */
	X2_SM_RESEND_INCOMPLETE		/* an unacked stanza has no copy */
} X2SmResend;

/**
 * Applies the h of <resumed h/> and lists what must be sent again: every
 * stanza in (h, outbound], oldest first.  The items stay owned by the
 * state (they are trimmed by later acks as usual) and keep their sequence
 * numbers: a resend is a retransmission, not a new stanza.
 *
 * @param trimmed  Set to the number of items h acknowledged (may be NULL).
 * @param resend   Set to a list of the queued items (free the list only),
 *                 NULL unless X2_SM_RESEND_OK.
 * @return X2_SM_RESEND_OK; X2_SM_RESEND_H_TOO_HIGH with the state
 *         unchanged; X2_SM_RESEND_INCOMPLETE (h applied) when an unacked
 *         stanza was evicted or queued without a copy (NULL item), so the
 *         stream cannot be resumed without losing it.
 */
X2SmResend x2_sm_state_resumed(X2SmState *st, guint32 h, guint *trimmed,
                               GList **resend);

/**
 * Removes every queued item and returns them oldest first; the caller owns
 * the list and the items.  Counters are left unchanged.
 */
GList *x2_sm_state_take_queue(X2SmState *st);

#endif /* XMPP2_CORE_SMSTATE_H_ */
