/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file msgctx.h X2Msg: context of one inbound message
 *
 * Every <message/> goes through x2_msg_receive(): a context is built, the
 * feature modules get to inspect or consume it (ext.h), and what is left is
 * delivered by jabber_message_deliver() in message.c.
 *
 * Ownership: a context built by x2_msg_receive() borrows the stanza from the
 * parser and is freed when the pipeline returns, whatever the verdict.  A
 * module that wants to keep a message (X2_DEFERRED) takes an x2_msg_dup(),
 * which owns a private copy of the stanza, and later passes it to
 * x2_msg_redeliver(), which consumes it.
 */
#ifndef XMPP2_EXT_MSGCTX_H_
#define XMPP2_EXT_MSGCTX_H_

#include <time.h>
#include <glib.h>
#include "xmlnode.h"

#include "ext/x2types.h"
#include "jabber.h"

typedef enum {
	X2_SRC_LIVE,    /* received on the stream */
	X2_SRC_CARBON,  /* unwrapped from a XEP-0280 carbon */
	X2_SRC_MAM      /* unwrapped from a XEP-0313 archive result */
} X2Source;

typedef enum {
	X2_DIR_IN,      /* sent by somebody else */
	X2_DIR_OUT      /* sent by us, from this or another device */
} X2Dir;

struct _X2Msg {
	xmlnode *outer;     /* stanza as received; owned only if owns_outer */
	xmlnode *msg;       /* the message to show: outer, or a node inside it */
	gboolean owns_outer; /* TRUE for x2_msg_dup() copies */
	int stage;          /* registry index of the module that saw it last,
	                     * -1 before the pipeline ran (ext.c) */
	X2Source src;
	X2Dir dir;
	time_t ts;          /* delay stamp or time of receipt */
	gboolean delayed;
	char *archive;      /* own bare JID, or room JID for groupchat */
	char *stanza_id;    /* trusted stanza-id only, see core/sid.h */
	char *key;          /* dedup key, see core/dedup.h, NULL if none */
	char *peer;         /* conversation key: bare JID, full JID for MUC PMs */
};

/**
 * Builds the default context (LIVE, IN) of a received stanza.  The archive
 * is our own bare JID, or the room for type='groupchat'.
 * Free with x2_msg_free().
 */
X2Msg *x2_msg_new(JabberStream *js, xmlnode *packet);

/** Frees a context, and its stanza copy if it owns one. NULL is allowed. */
void x2_msg_free(X2Msg *m);

/**
 * Deep copy for modules that defer a message (X2_DEFERRED): the stanza is
 * copied with xmlnode_copy(), msg points into the copy, and the result owns
 * both.  Hand it back with x2_msg_redeliver().
 *
 * @return The copy, or NULL with a critical if m->msg is not inside m->outer.
 */
X2Msg *x2_msg_dup(const X2Msg *m);

/**
 * Resumes a deferred copy: runs the inbound hooks after the module that
 * deferred it (m->stage), delivers it if they all pass, and frees it in
 * every case.
 */
void x2_msg_redeliver(JabberStream *js, X2Msg *m);

/**
 * Re-targets a context to a message found inside the stanza, as carbons and
 * MAM do after unwrapping: sets src, dir and archive and recomputes ts, peer,
 * stanza_id and key from the inner message.  The delay on the enclosing
 * <forwarded/> wins over a delay on the inner message.
 *
 * @param archive  Archive JID (MAM), or NULL for the default: our own bare
 *                 JID, or the room for type='groupchat'.
 */
void x2_msg_set_inner(JabberStream *js, X2Msg *m, xmlnode *inner,
                      X2Source src, X2Dir dir, const char *archive);

/** Entry point for every received <message/>. */
void x2_msg_receive(JabberStream *js, xmlnode *packet);

/**
 * TRUE for messages that happen now and come from somebody else.  Chat
 * states are only meaningful for those.
 */
gboolean x2_msg_is_live_incoming(const X2Msg *m);

/**
 * Writes a message that we sent from another device (dir == X2_DIR_OUT)
 * into the IM conversation of m->peer with SEND | REMOTE_SEND.
 *
 * @param body  Message text, already in libpurple's HTML form.
 */
void x2_msg_write_outgoing(JabberStream *js, const X2Msg *m, const char *body);

/**
 * Stamps an outgoing <message/>: a fresh UUID becomes both the stanza id and
 * the XEP-0359 origin-id, then the outbound hooks run.
 */
void x2_msg_prepare_outgoing(JabberStream *js, xmlnode *message);

#endif /* XMPP2_EXT_MSGCTX_H_ */
