/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file carbon_parse.h XEP-0280 message carbons: classify and validate
 *
 * Pure logic: <glib.h> and xmlnode.h only.  Carbons are the one place where
 * a message that claims to come from a contact is really a wrapper from our
 * own server, so every forged shape is rejected here (ARCHITECTURE 8).
 */
#ifndef XMPP2_CORE_CARBON_PARSE_H_
#define XMPP2_CORE_CARBON_PARSE_H_

#include <glib.h>
#include "xmlnode.h"

#define X2_NS_CARBONS "urn:xmpp:carbons:2"

typedef enum {
	/* not a carbon: process as an ordinary message */
	X2_CARBON_NONE,
	/* <received/>: somebody wrote to one of our other resources */
	X2_CARBON_RECEIVED,
	/* <sent/>: we wrote from one of our other resources */
	X2_CARBON_SENT,
	/* a carbon that fails a check: drop it, log the reason */
	X2_CARBON_REJECTED
} X2CarbonKind;

/**
 * Classifies a <message/> and validates it when it is a carbon.
 *
 * Accepted only when
 *  - the outer "from" is absent or our bare JID exactly (no resource),
 *  - there is exactly one <sent/> or <received/> in the carbons namespace,
 *    holding a <forwarded/> with a <message/> of type chat or normal,
 *  - for <sent/> the inner "from" has our bare JID and an inner "to" exists,
 *  - for <received/> the inner "to" has our bare JID and an inner "from"
 *    exists.
 * Bare JIDs compare case-insensitively (ASCII).
 *
 * @param outer      The <message/> as received.
 * @param own_bare   Our bare JID.
 * @param inner      Receives the wrapped <message/> (owned by @a outer) for
 *                   RECEIVED and SENT, NULL otherwise.  May be NULL.
 * @param reason     Receives a static string describing the failure for
 *                   REJECTED, NULL otherwise.  May be NULL.
 */
X2CarbonKind x2_carbon_classify(const xmlnode *outer, const char *own_bare,
                                xmlnode **inner, const char **reason);

#endif /* XMPP2_CORE_CARBON_PARSE_H_ */
