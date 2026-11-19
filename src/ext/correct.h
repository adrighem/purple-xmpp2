/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file correct.h XEP-0308 Last Message Correction (libpurple glue)
 *
 * correct.c: registry entry, inbound check and marking, outbound hook.
 * correct_cmd.c: the /correct command.  The pure logic is in
 * core/replace_parse.c and core/corrlog.c.
 */
#ifndef XMPP2_EXT_CORRECT_H_
#define XMPP2_EXT_CORRECT_H_

#include <glib.h>

#include "core/corrlog.h"
#include "ext/ext.h"

/** Registry entry, listed in the table in ext.c. */
extern const X2Ext x2_ext_correct;

/** Message ids remembered per stream for the sender check. */
#define X2_CORRECT_LOG_SIZE 2048

/** Per-stream state of the module (priv slot). */
typedef struct {
	X2CorrLog *log;
	/* id the next outgoing message with a body corrects, set only while
	 * the /correct command sends its text */
	char *pending;
} X2CorrectState;

X2CorrectState *x2_correct_state(JabberStream *js);

/**
 * Conversation key of a JID we send to: the bare JID, except for a private
 * message to a room occupant, which keeps the full JID (as in msgctx.c).
 *
 * @return A newly allocated string, or NULL for an invalid JID.
 */
char *x2_correct_peer(JabberStream *js, const char *jid, gboolean groupchat);

/** Registers and unregisters the /correct command. */
void x2_correct_cmd_register(void);
void x2_correct_cmd_unregister(void);

#endif /* XMPP2_EXT_CORRECT_H_ */
