/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file peerutil.h Helpers shared by the receipts and markers modules
 */
#ifndef XMPP2_EXT_PEERUTIL_H_
#define XMPP2_EXT_PEERUTIL_H_

#include <glib.h>

#include "jabber.h"

/** Account option: show delivery and read notices in the conversation. */
#define X2_PREF_NOTICES "xmpp2_receipt_notices"

/**
 * Should an outgoing message to @a jid carry an element of namespace @a ns
 * (receipt request, markable)?  Yes unless @a jid is a room occupant, or its
 * resource is known from caps and does not advertise @a ns.  Unknown caps
 * count as supported (XEP-0184 4).
 */
gboolean x2_peer_wants(JabberStream *js, const char *jid, const char *ns);

/** TRUE if @a jid is a member of a room we have joined. */
gboolean x2_peer_is_occupant(JabberStream *js, const char *jid);

/**
 * Writes a system line (not logged) into the IM conversation with @a jid,
 * if the account option X2_PREF_NOTICES is on and the conversation exists.
 */
void x2_peer_notice(JabberStream *js, const char *jid, const char *text);

#endif /* XMPP2_EXT_PEERUTIL_H_ */
