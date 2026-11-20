/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file tickmap.h Tick states, glyphs and a bounded id map (pure logic)
 *
 * The delivery state of a sent message only ever moves forward: none,
 * delivered (XEP-0184 receipt or a "received" marker), displayed (a
 * "displayed" or "acknowledged" marker).  X2TickMap maps message ids to
 * caller data and keeps at most @a capacity entries, dropping the least
 * recently used one.
 */
#ifndef XMPP2_TICKS_TICKMAP_H_
#define XMPP2_TICKS_TICKMAP_H_

#include <glib.h>

typedef enum {
	X2_TICK_NONE = 0,
	X2_TICK_DELIVERED,
	X2_TICK_DISPLAYED
} X2TickState;

typedef enum {
	X2_TICK_STYLE_CHECKS = 0, /* U+2713 and two of them */
	X2_TICK_STYLE_ASCII,      /* "+" and "++" */
	X2_TICK_STYLE_WORDS       /* "(delivered)" and "(read)" */
} X2TickStyle;

/** State a XEP-0333 marker name stands for; X2_TICK_NONE if unknown. */
X2TickState x2_tick_state_from_marker(const char *name);

/** The later of two states: states never go back. */
X2TickState x2_tick_state_merge(X2TickState a, X2TickState b);

/** Style from its preference string; unknown or NULL gives CHECKS. */
X2TickStyle x2_tick_style_from_string(const char *s);

/** Static glyph for a state; NULL for X2_TICK_NONE. */
const char *x2_tick_glyph(X2TickStyle style, X2TickState state);

typedef struct _X2TickMap X2TickMap;

/** Map holding at most @a capacity ids (> 0); @a value_free may be NULL. */
X2TickMap *x2_tick_map_new(guint capacity, GDestroyNotify value_free);

/** Frees the map and every value. NULL is allowed. */
void x2_tick_map_free(X2TickMap *map);

guint x2_tick_map_size(const X2TickMap *map);

/**
 * Stores @a value under @a id as the most recently used entry.  An old
 * value of the same id is freed; past the capacity the least recently used
 * entry is freed.
 */
void x2_tick_map_insert(X2TickMap *map, const char *id, gpointer value);

/** Value of @a id or NULL; a hit becomes the most recently used entry. */
gpointer x2_tick_map_lookup(X2TickMap *map, const char *id);

/** Removes and frees @a id; FALSE if it was not there. */
gboolean x2_tick_map_remove(X2TickMap *map, const char *id);

/**
 * Removes and frees every entry for which @a func returns TRUE.
 *
 * @return the number of entries removed
 */
guint x2_tick_map_foreach_remove(X2TickMap *map, GHRFunc func,
                                 gpointer data);

/** Removes and frees every entry. */
void x2_tick_map_clear(X2TickMap *map);

#endif /* XMPP2_TICKS_TICKMAP_H_ */
