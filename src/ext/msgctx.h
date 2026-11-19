/**
 * @file msgctx.h X2Msg: context of one inbound message
 *
 * Every <message/> goes through x2_msg_receive(): a context is built, the
 * feature modules get to inspect or consume it (ext.h), and what is left is
 * delivered by jabber_message_deliver() in message.c.
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
	xmlnode *outer;     /* stanza as received (not owned) */
	xmlnode *msg;       /* the message to show: outer, or the inner one (not owned) */
	X2Source src;
	X2Dir dir;
	time_t ts;          /* delay stamp or time of receipt */
	gboolean delayed;
	char *archive;      /* own bare JID or room JID, NULL for LIVE */
	char *stanza_id;    /* trusted stanza-id only, see core/sid.h */
	char *key;          /* dedup key, see core/dedup.h, NULL if none */
	char *peer;         /* conversation key: bare JID, full JID for MUC PMs */
};

/**
 * Builds the default context (LIVE, IN) of a received stanza.
 * Free with x2_msg_free().
 */
X2Msg *x2_msg_new(JabberStream *js, xmlnode *packet);

void x2_msg_free(X2Msg *m);

/**
 * Re-targets a context to a message found inside the stanza, as carbons and
 * MAM do after unwrapping: sets src, dir and archive and recomputes ts, peer,
 * stanza_id and key from the inner message.
 *
 * @param archive  Archive JID for MAM, or NULL.
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
