/**
 * @file dedup.h Bounded LRU set of message keys (pure logic)
 *
 * A message is identified by a string key, see x2_dedup_make_key().  The set
 * remembers the most recent @a capacity keys; adding a new key past the
 * capacity evicts the least recently used one.
 */
#ifndef XMPP2_CORE_DEDUP_H_
#define XMPP2_CORE_DEDUP_H_

#include <glib.h>

typedef struct _X2Dedup X2Dedup;

/** Creates a set holding at most @a capacity keys (capacity must be > 0). */
X2Dedup *x2_dedup_new(guint capacity);

/** Frees the set and all keys. NULL is allowed. */
void x2_dedup_free(X2Dedup *d);

/** Number of keys currently stored. */
guint x2_dedup_size(const X2Dedup *d);

/** Checks for a key without changing its recency. */
gboolean x2_dedup_contains(const X2Dedup *d, const char *key);

/**
 * Adds a key, or marks it as most recently used if it is already present.
 *
 * @return TRUE if the key was new, FALSE if it was already in the set.
 */
gboolean x2_dedup_add(X2Dedup *d, const char *key);

/**
 * Builds the dedup key of a message (ARCHITECTURE 7.3).
 *
 * Preference order: a trusted stanza-id ("S:<id>"), else the origin-id
 * ("O:<dir>:<peer>:<id>"), else the stanza @@id attribute
 * ("I:<dir>:<peer>:<id>").
 *
 * @param trusted_stanza_id  Stanza-id already verified against the archive, or NULL.
 * @param outgoing           TRUE if we sent the message from any of our devices.
 * @param peer               Conversation key (bare JID, or full JID for MUC PMs).
 * @param origin_id          XEP-0359 origin-id, or NULL.
 * @param id_attr            The id attribute of the stanza, or NULL.
 * @return A newly allocated key, or NULL if the message has no usable id.
 */
char *x2_dedup_make_key(const char *trusted_stanza_id, gboolean outgoing,
                        const char *peer, const char *origin_id,
                        const char *id_attr);

#endif /* XMPP2_CORE_DEDUP_H_ */
