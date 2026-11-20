/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
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
 * Preference order: a trusted stanza-id ("S:<archive>:<id>": the id is
 * unique within its archive only), else the origin-id
 * ("O:<dir>:<peer>:<id>").  The stanza @@id attribute is never used: a
 * foreign id is not unique enough to key on, and every message we send
 * carries an origin-id, so "O:" already covers our own sends.
 *
 * @param trusted_stanza_id  Stanza-id already verified against the archive, or NULL.
 * @param archive            Bare JID of the archive that issued it (our own
 *                           bare JID, or the room); a stanza-id without one
 *                           is not usable.
 * @param outgoing           TRUE if we sent the message from any of our devices.
 * @param peer               Conversation key (bare JID, or full JID for MUC PMs).
 * @param origin_id          XEP-0359 origin-id, or NULL.
 * @return A newly allocated key, or NULL if the message has no usable id
 *         (it is then never treated as a duplicate).
 */
char *x2_dedup_make_key(const char *trusted_stanza_id, const char *archive,
                        gboolean outgoing, const char *peer,
                        const char *origin_id);

#endif /* XMPP2_CORE_DEDUP_H_ */
