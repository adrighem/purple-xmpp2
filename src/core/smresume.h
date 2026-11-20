/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file smresume.h XEP-0198 resumption policy (pure logic)
 *
 * Decides whether and when a lost transport is reconnected for <resume/>:
 * a bounded number of attempts with backoff, all inside the window the
 * server promised in <enabled max/> (capped).  Times are monotonic
 * microseconds (g_get_monotonic_time()), delays are milliseconds.
 */
#ifndef XMPP2_CORE_SMRESUME_H_
#define XMPP2_CORE_SMRESUME_H_

#include <glib.h>

/** Reconnect attempts per transport loss. */
#define X2_SM_RESUME_ATTEMPTS 2
/** Delay before the first attempt, then before each further attempt. */
#define X2_SM_RESUME_FIRST_DELAY_MS 1000
#define X2_SM_RESUME_RETRY_DELAY_MS 5000
/** One attempt (connect, TLS, SASL, <resumed/>) must finish in this time. */
#define X2_SM_RESUME_ATTEMPT_TIMEOUT_MS 20000
/** Window when <enabled/> has no max, and the cap on any server max. */
#define X2_SM_RESUME_DEFAULT_WINDOW_S 60
#define X2_SM_RESUME_MAX_WINDOW_S 600

typedef enum {
	X2_SM_RESUME_IDLE = 0,	/* connected, or never lost */
	X2_SM_RESUME_WAITING,	/* transport down, next attempt scheduled */
	X2_SM_RESUME_ATTEMPTING	/* reconnecting, <resume/> pending */
} X2SmResumePhase;

typedef struct {
	X2SmResumePhase phase;
	guint attempts;		/* attempts started since the loss */
	gint64 deadline;	/* monotonic us: no attempt starts after this */
} X2SmResume;

/** The window in seconds for a server max (0: none given). */
guint x2_sm_resume_window(guint server_max);

/**
 * The transport was lost at @a now.  @a first_delay_ms is normally
 * X2_SM_RESUME_FIRST_DELAY_MS (tests may stretch it).
 *
 * @return The delay before the first attempt, or -1: do not resume (not
 *         resumable, or already resuming).
 */
gint x2_sm_resume_lost(X2SmResume *r, gboolean resumable, guint server_max,
                       guint first_delay_ms, gint64 now);

/**
 * The delay timer fired: TRUE starts an attempt (phase ATTEMPTING);
 * FALSE means the window is over, give up (phase IDLE).
 */
gboolean x2_sm_resume_start(X2SmResume *r, gint64 now);

/**
 * The running attempt failed (transport error or timeout).
 *
 * @return The delay before the next attempt (phase WAITING), or -1: give
 *         up (phase IDLE), no attempts left or the window is over.
 */
gint x2_sm_resume_failed(X2SmResume *r, gint64 now);

/** Resumed, or gave up: back to IDLE. */
void x2_sm_resume_reset(X2SmResume *r);

#endif /* XMPP2_CORE_SMRESUME_H_ */
