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

#endif /* XMPP2_CORE_FORWARD_H_ */
