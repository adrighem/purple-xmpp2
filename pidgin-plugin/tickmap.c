/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file tickmap.c Tick states, glyphs and a bounded id map (pure logic)
 */
#include "tickmap.h"

#include <string.h>

/* Entries in recency order: head is the least recently used. */
struct _X2TickMap {
	guint capacity;
	GDestroyNotify value_free;
	GHashTable *index;  /* id (owned by the entry) -> GList link in order */
	GQueue order;       /* of Entry */
};

typedef struct {
	char *id;
	gpointer value;
} Entry;

X2TickState
x2_tick_state_from_marker(const char *name)
{
	if (name == NULL)
		return X2_TICK_NONE;
	if (strcmp(name, "received") == 0)
		return X2_TICK_DELIVERED;
	if (strcmp(name, "displayed") == 0 || strcmp(name, "acknowledged") == 0)
		return X2_TICK_DISPLAYED;
	return X2_TICK_NONE;
}

X2TickState
x2_tick_state_merge(X2TickState a, X2TickState b)
{
	return a > b ? a : b;
}

X2TickStyle
x2_tick_style_from_string(const char *s)
{
	if (g_strcmp0(s, "ascii") == 0)
		return X2_TICK_STYLE_ASCII;
	if (g_strcmp0(s, "words") == 0)
		return X2_TICK_STYLE_WORDS;
	return X2_TICK_STYLE_CHECKS;
}

const char *
x2_tick_glyph(X2TickStyle style, X2TickState state)
{
	static const char *const glyphs[][2] = {
		[X2_TICK_STYLE_CHECKS] = { "\xe2\x9c\x93", "\xe2\x9c\x93\xe2\x9c\x93" },
		[X2_TICK_STYLE_ASCII] = { "+", "++" },
		[X2_TICK_STYLE_WORDS] = { "(delivered)", "(read)" },
	};

	if (state == X2_TICK_NONE || (guint)style >= G_N_ELEMENTS(glyphs))
		return NULL;
	return glyphs[style][state == X2_TICK_DISPLAYED];
}

X2TickMap *
x2_tick_map_new(guint capacity, GDestroyNotify value_free)
{
	X2TickMap *map;

	g_return_val_if_fail(capacity > 0, NULL);

	map = g_new0(X2TickMap, 1);
	map->capacity = capacity;
	map->value_free = value_free;
	map->index = g_hash_table_new(g_str_hash, g_str_equal);
	g_queue_init(&map->order);
	return map;
}

static void
entry_free(X2TickMap *map, Entry *e)
{
	if (map->value_free != NULL && e->value != NULL)
		map->value_free(e->value);
	g_free(e->id);
	g_free(e);
}

/* Unlinks first, so value_free may look at the map without seeing @a link. */
static void
remove_link(X2TickMap *map, GList *link)
{
	Entry *e = link->data;

	g_hash_table_remove(map->index, e->id);
	g_queue_delete_link(&map->order, link);
	entry_free(map, e);
}

void
x2_tick_map_clear(X2TickMap *map)
{
	g_return_if_fail(map != NULL);

	while (map->order.head != NULL)
		remove_link(map, map->order.head);
}

void
x2_tick_map_free(X2TickMap *map)
{
	if (map == NULL)
		return;
	x2_tick_map_clear(map);
	g_hash_table_destroy(map->index);
	g_free(map);
}

guint
x2_tick_map_size(const X2TickMap *map)
{
	g_return_val_if_fail(map != NULL, 0);
	return map->order.length;
}

void
x2_tick_map_insert(X2TickMap *map, const char *id, gpointer value)
{
	Entry *e;
	GList *link;

	g_return_if_fail(map != NULL && id != NULL);

	link = g_hash_table_lookup(map->index, id);
	if (link != NULL)
		remove_link(map, link);
	while (map->order.length >= map->capacity)
		remove_link(map, map->order.head);

	e = g_new0(Entry, 1);
	e->id = g_strdup(id);
	e->value = value;
	g_queue_push_tail(&map->order, e);
	g_hash_table_insert(map->index, e->id, map->order.tail);
}

gpointer
x2_tick_map_lookup(X2TickMap *map, const char *id)
{
	GList *link;

	g_return_val_if_fail(map != NULL, NULL);
	if (id == NULL)
		return NULL;

	link = g_hash_table_lookup(map->index, id);
	if (link == NULL)
		return NULL;
	g_queue_unlink(&map->order, link);
	g_queue_push_tail_link(&map->order, link);
	return ((Entry *)link->data)->value;
}

gboolean
x2_tick_map_remove(X2TickMap *map, const char *id)
{
	GList *link;

	g_return_val_if_fail(map != NULL, FALSE);
	if (id == NULL)
		return FALSE;

	link = g_hash_table_lookup(map->index, id);
	if (link == NULL)
		return FALSE;
	remove_link(map, link);
	return TRUE;
}

guint
x2_tick_map_foreach_remove(X2TickMap *map, GHRFunc func, gpointer data)
{
	GList *link, *next;
	guint removed = 0;

	g_return_val_if_fail(map != NULL && func != NULL, 0);

	for (link = map->order.head; link != NULL; link = next) {
		Entry *e = link->data;

		next = link->next;
		if (func(e->id, e->value, data)) {
			remove_link(map, link);
			removed++;
		}
	}
	return removed;
}
