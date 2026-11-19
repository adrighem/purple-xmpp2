/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file store.h Persistent archive state: cursors and key rings (pure logic)
 *
 * One GKeyFile per account (ARCHITECTURE 9):
 *
 *     [archive user@example.org]
 *     cursor=1a2b...       last delivered stanza-id
 *     cursor_ts=1763540000 its timestamp, fallback for item-not-found
 *     ring=O:out:bob@x:id1;...   last X2_STORE_RING_MAX non-S: keys
 *     [meta]
 *     initial=cursor-only  set by migration: first sync shows nothing
 *     version=1
 *
 * A missing file is an empty state; a corrupt one too, with a warning.
 */
#ifndef XMPP2_CORE_STORE_H_
#define XMPP2_CORE_STORE_H_

#include <glib.h>

/** Keys kept per archive in the ring. */
#define X2_STORE_RING_MAX 256

typedef struct _X2Store X2Store;

/** An empty state. */
X2Store *x2_store_new(void);

/**
 * Loads a state file.  A missing file yields an empty state silently, an
 * unreadable or corrupt one an empty state plus a g_warning().
 */
X2Store *x2_store_load(const char *path);

/** Serialises the state (GKeyFile text). Free with g_free(). */
char *x2_store_to_data(const X2Store *s, gsize *len);

/**
 * Writes the state atomically (temp file plus rename), mode 0600.
 * Clears the dirty flag on success.
 */
gboolean x2_store_save(X2Store *s, const char *path, GError **error);

void x2_store_free(X2Store *s);

/** TRUE if the state changed since it was loaded or last saved. */
gboolean x2_store_is_dirty(const X2Store *s);

/**
 * The cursor of an archive.
 *
 * @param ts  Optionally receives cursor_ts (0 if unknown).
 * @return The stanza-id (owned by the store) or NULL.
 */
const char *x2_store_get_cursor(const X2Store *s, const char *archive,
                                gint64 *ts);

/** Sets the cursor of an archive (@a ts 0 keeps the previous timestamp). */
void x2_store_set_cursor(X2Store *s, const char *archive, const char *id,
                         gint64 ts);

/** Appends a key to the ring of an archive, dropping the oldest past the cap. */
void x2_store_ring_add(X2Store *s, const char *archive, const char *key);

/** TRUE if the ring of the archive holds @a key. */
gboolean x2_store_ring_contains(const X2Store *s, const char *archive,
                                const char *key);

/**
 * Calls @a fn for every ring key of every archive, oldest first, for
 * seeding the in-memory dedup set.
 */
void x2_store_ring_foreach(const X2Store *s, GFunc fn, gpointer data);

/** TRUE if [meta] initial=cursor-only (cloned account). */
gboolean x2_store_cursor_only(const X2Store *s);
void x2_store_set_cursor_only(X2Store *s, gboolean on);

#endif /* XMPP2_CORE_STORE_H_ */
