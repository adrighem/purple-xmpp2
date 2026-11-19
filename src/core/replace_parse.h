/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file replace_parse.h XEP-0308 correction element: parse, build, mark
 *
 * Only <glib.h> and libpurple's xmlnode.h are used, no connection state, so
 * this file is unit tested without a running libpurple core.
 */
#ifndef XMPP2_CORE_REPLACE_PARSE_H_
#define XMPP2_CORE_REPLACE_PARSE_H_

#include <glib.h>
#include "xmlnode.h"

#define X2_NS_REPLACE "urn:xmpp:message-correct:0"
#define X2_NS_OCCUPANT_ID "urn:xmpp:occupant-id:0"

/** Prefix of the shown text of an accepted correction. */
#define X2_REPLACE_MARKER "corrected: "

/**
 * Returns the id of the first <replace/> child of a message.
 *
 * @return A newly allocated string, or NULL if absent or empty.
 */
char *x2_replace_id(const xmlnode *message);

/** Adds <replace id='ID'/> to a message. */
void x2_replace_add(xmlnode *message, const char *id);

/**
 * Identity of the author of a message, for the XEP-0308 sender check.
 *
 * Groupchat: the XEP-0421 occupant-id ("occ:ID") when the room stamped one,
 * else the full JID (room/nick).  Anything else: the bare JID of @a from.
 *
 * @param from       The "from" of the message (of the sender, for carbons).
 * @param groupchat  TRUE for type='groupchat'.
 * @param trust_occupant_id  The room advertises urn:xmpp:occupant-id:0, so it
 *                   strips forged ones.  Without that, any occupant could
 *                   claim another's id, and the id is ignored.
 * @return A newly allocated string, or NULL if @a from is NULL or empty.
 */
char *x2_replace_sender(const xmlnode *message, const char *from,
                        gboolean groupchat, gboolean trust_occupant_id);

/**
 * Marks a message as an accepted correction: prefixes the text of <body/>
 * with @a marker and drops the XHTML-IM alternative, which would otherwise
 * be shown instead of the marked text.
 *
 * @param marker  The prefix, translated (X2_REPLACE_MARKER in English).
 * @return FALSE (message unchanged) if there is no non-empty <body/>.
 */
gboolean x2_replace_mark_body(xmlnode *message, const char *marker);

#endif /* XMPP2_CORE_REPLACE_PARSE_H_ */
