/**
 * @file sid.h XEP-0359 unique and stable stanza ids (pure logic)
 *
 * Only <glib.h> and libpurple's xmlnode.h are used, no connection state, so
 * this file is unit tested without a running libpurple core.
 */
#ifndef XMPP2_CORE_SID_H_
#define XMPP2_CORE_SID_H_

#include <glib.h>
#include "xmlnode.h"

#define X2_NS_SID "urn:xmpp:sid:0"

/**
 * Creates a fresh random id (UUID version 4) for origin-id and stanza ids.
 *
 * @return A newly allocated string, free with g_free().
 */
char *x2_sid_new_id(void);

/**
 * Returns the id of the first <origin-id/> child of a message.
 *
 * @return A newly allocated string, or NULL if absent or empty.
 */
char *x2_sid_origin_id(const xmlnode *message);

/**
 * Returns the id of the <stanza-id/> child stamped by @a archive.
 *
 * Stanza ids are only trustworthy when the "by" attribute names the entity
 * we trust (the archive that was queried, or our own account for the account
 * archive).  A stanza-id with any other "by" is ignored, as XEP-0359 requires,
 * so a forged one cannot influence deduplication or cursors.
 *
 * @param message  The <message/> stanza (the inner one for carbons or MAM).
 * @param archive  The exact JID that must appear in "by".
 * @return A newly allocated string, or NULL if there is no trusted one.
 */
char *x2_sid_stanza_id(const xmlnode *message, const char *archive);

/**
 * Adds <origin-id id='ID'/> to a message.
 *
 * @param message  The <message/> to extend.
 * @param id       The id to use.
 */
void x2_sid_add_origin_id(xmlnode *message, const char *id);

#endif /* XMPP2_CORE_SID_H_ */
