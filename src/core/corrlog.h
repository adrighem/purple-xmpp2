/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file corrlog.h Who wrote which message id (XEP-0308 sender check)
 *
 * A bounded log of (conversation, message id) -> author, plus the id of the
 * last message we sent per conversation for the /correct command.  Pure
 * logic, no libpurple.
 */
#ifndef XMPP2_CORE_CORRLOG_H_
#define XMPP2_CORE_CORRLOG_H_

#include <glib.h>

typedef struct _X2CorrLog X2CorrLog;

typedef enum {
	X2_CORR_OK,        /* original known, same author */
	X2_CORR_UNKNOWN,   /* original never seen in this conversation */
	X2_CORR_MISMATCH   /* original known, other author: forged */
} X2CorrVerdict;

/** Keeps at most @a capacity message ids, the oldest are forgotten first. */
X2CorrLog *x2_corrlog_new(guint capacity);
void x2_corrlog_free(X2CorrLog *log);

/** Remembers that @a sender wrote message @a id in conversation @a peer. */
void x2_corrlog_record(X2CorrLog *log, const char *peer, const char *id,
                       const char *sender);

/** Checks a correction of @a id sent by @a sender in conversation @a peer. */
X2CorrVerdict x2_corrlog_check(const X2CorrLog *log, const char *peer,
                               const char *id, const char *sender);

/** Sets the id of the last message we sent in @a peer. */
void x2_corrlog_set_last_sent(X2CorrLog *log, const char *peer,
                              const char *id);

/** @return The id, owned by the log, or NULL if we sent nothing there. */
const char *x2_corrlog_last_sent(const X2CorrLog *log, const char *peer);

#endif /* XMPP2_CORE_CORRLOG_H_ */
