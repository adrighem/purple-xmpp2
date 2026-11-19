/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file receipt_parse.h XEP-0184 receipts and XEP-0333 chat markers (pure logic)
 *
 * Parse and build the protocol elements, decide which messages may be
 * answered.  Only <glib.h> and libpurple's xmlnode.h are used, no connection
 * state, so this file is unit tested without a running libpurple core.
 */
#ifndef XMPP2_CORE_RECEIPT_PARSE_H_
#define XMPP2_CORE_RECEIPT_PARSE_H_

#include <glib.h>
#include "xmlnode.h"

#define X2_NS_RECEIPTS "urn:xmpp:receipts"
#define X2_NS_CHAT_MARKERS "urn:xmpp:chat-markers:0"

typedef enum {
	X2_MARKER_NONE,
	X2_MARKER_RECEIVED,
	X2_MARKER_DISPLAYED,
	X2_MARKER_ACKNOWLEDGED
} X2MarkerType;

/**
 * Returns the stanza id of a message that asks for a delivery receipt and
 * may be answered: type chat, normal or absent, a non-empty id, a
 * <request/> child, and not itself a receipt (XEP-0184 5.4).
 *
 * @return A newly allocated string, or NULL.
 */
char *x2_receipt_request_id(const xmlnode *message);

/** Returns the id of the <received/> child of a message, or NULL. */
char *x2_receipt_received_id(const xmlnode *message);

/** Adds <request xmlns='urn:xmpp:receipts'/> to a message. */
void x2_receipt_add_request(xmlnode *message);

/**
 * Builds <message to=TO><received id=ID/></message>, the answer to a request.
 *
 * @return A new node, free with xmlnode_free().
 */
xmlnode *x2_receipt_new_received(const char *to, const char *id);

/**
 * Returns the stanza id of a message that may be marked: type chat, normal
 * or absent, a non-empty id and a <markable/> child.
 *
 * @return A newly allocated string, or NULL.
 */
char *x2_marker_markable_id(const xmlnode *message);

/**
 * Finds a received, displayed or acknowledged marker in a message.
 *
 * @param id  Set to the id of the marked message (free with g_free()), only
 *            when a marker is found.
 * @return The marker type, or X2_MARKER_NONE.
 */
X2MarkerType x2_marker_parse(const xmlnode *message, char **id);

/** Element name of a marker type ("displayed"), NULL for X2_MARKER_NONE. */
const char *x2_marker_name(X2MarkerType type);

/** Adds <markable xmlns='urn:xmpp:chat-markers:0'/> to a message. */
void x2_marker_add_markable(xmlnode *message);

/**
 * Builds <message to=TO type='chat'><displayed id=ID/></message> (or
 * received, acknowledged).
 *
 * @return A new node, free with xmlnode_free(), or NULL for X2_MARKER_NONE.
 */
xmlnode *x2_marker_new(const char *to, X2MarkerType type, const char *id);

/**
 * Key for remembering that we asked @a jid to acknowledge message @a id
 * (receipt or marker): "<bare jid>\x1f<id>", so an answer from anybody else
 * does not match.  The resource and the case of the JID do not matter.
 *
 * @return A newly allocated string.
 */
char *x2_ack_key(const char *jid, const char *id);

#endif /* XMPP2_CORE_RECEIPT_PARSE_H_ */
