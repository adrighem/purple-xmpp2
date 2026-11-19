/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file forward.h XEP-0297 stanza forwarding, XEP-0203 delayed delivery
 *
 * Pure logic: <glib.h>, xmlnode.h and util.h only.
 */
#ifndef XMPP2_CORE_FORWARD_H_
#define XMPP2_CORE_FORWARD_H_

#include <time.h>
#include <glib.h>
#include "xmlnode.h"

#define X2_NS_FORWARD "urn:xmpp:forward:0"

/**
 * Finds the <message/> wrapped in a <forwarded/> element.
 *
 * @param container  An element holding a <forwarded xmlns='urn:xmpp:forward:0'/>
 *                   child, such as a carbons <sent/> or a MAM <result/>.
 * @return The inner <message/> (owned by the tree, not copied) or NULL.
 */
xmlnode *x2_forward_get_message(const xmlnode *container);

/**
 * Finds the <forwarded/> child itself (for its <delay/>).
 *
 * @return The element (not copied) or NULL.
 */
xmlnode *x2_forward_get_forwarded(const xmlnode *container);

/**
 * Parses the delay stamp of a stanza or <forwarded/> element.
 *
 * Looks for a XEP-0203 <delay xmlns='urn:xmpp:delay'/> child first and falls
 * back to the legacy <x xmlns='jabber:x:delay'/>.
 *
 * @param stanza  The element to inspect.
 * @param ts      Receives the time as seconds since the epoch.
 * @param from    Optionally receives the "from" attribute of the delay
 *                (newly allocated, may be set to NULL), free with g_free().
 * @return TRUE if a valid stamp was found.
 */
gboolean x2_delay_parse(const xmlnode *stanza, time_t *ts, char **from);

/**
 * Parses the delay stamp that applies to a (possibly forwarded) message.
 *
 * Carbons and MAM results put the original send time on the enclosing
 * <forwarded><delay/></forwarded>, not on the wrapped message.  If the
 * parent of @a message is a <forwarded xmlns='urn:xmpp:forward:0'/> with a
 * valid delay, that stamp wins; otherwise the message's own delay is used.
 *
 * @param message  The message to show (outer, or inner of a <forwarded/>).
 * @param ts       Receives the time as seconds since the epoch.
 * @return TRUE if a valid stamp was found.
 */
gboolean x2_forward_delay_parse(const xmlnode *message, time_t *ts);

/**
 * Deep-copies a stanza and locates the copy of one of its descendants.
 *
 * Used to keep a forwarded message beyond the lifetime of the parsed
 * stanza: the copy of @a outer is returned and @a *node_copy points to the
 * element at the same position as @a node.
 *
 * @param outer      Root of the tree to copy.
 * @param node       @a outer itself or an element inside it.
 * @param node_copy  Receives the relocated @a node, owned by the returned tree.
 * @return The copy (free with xmlnode_free()), or NULL with a critical
 *         warning if @a node is not inside @a outer.
 */
xmlnode *x2_forward_copy(const xmlnode *outer, const xmlnode *node,
                         xmlnode **node_copy);

#endif /* XMPP2_CORE_FORWARD_H_ */
