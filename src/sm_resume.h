/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file sm_resume.h XEP-0198 resumption as a silent reconnect (M10)
 *
 * When the transport of a resumable session fails, the PurpleConnection
 * stays PURPLE_CONNECTED (buddies, chats and conversations untouched)
 * while the socket is rebuilt underneath: connect, TLS, SASL, then
 * <resume previd h/> instead of a bind.  Bounded attempts inside the
 * server's max (core/smresume.c).  On <failed/>, a server without SM, or
 * no attempts left, the usual network connection error is raised: the UI
 * reconnects with a full login, MAM fills the gap and the unacked
 * messages are reported (stream_management.c).
 */
#ifndef XMPP2_SM_RESUME_H_
#define XMPP2_SM_RESUME_H_

#include "jabber.h"

/*
 * The transport failed with @a why.  Returns TRUE when resumption handles
 * it (first loss of a resumable session, or a failed attempt), FALSE when
 * the caller must raise the connection error itself.
 */
gboolean jabber_sm_resume_lost(JabberStream *js, const char *why);

/* <stream:features/> on a reconnecting stream: SASL, then <resume/>. */
void jabber_sm_resume_features(JabberStream *js, xmlnode *packet);

/* Sends <resume previd h/> on the authenticated stream. */
void jabber_sm_send_resume(JabberStream *js);

/* <resumed h/>: trim what the server handled, resend the rest in order. */
void jabber_sm_resume_resumed(JabberStream *js, xmlnode *packet);

/* <failed/> to <resume/>: trim h if given, then give up. */
void jabber_sm_resume_failed(JabberStream *js, xmlnode *packet);

/* Stop resuming and raise a network connection error (@a why is shown). */
void jabber_sm_resume_give_up(JabberStream *js, const char *why);

/* Deliberate close (sign-off, account removal): cancel every timer. */
void jabber_sm_resume_cancel(JabberStream *js);

#endif /* XMPP2_SM_RESUME_H_ */
