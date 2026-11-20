/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/* Unit tests of pidgin-plugin/tickmap.c: states, glyphs, bounded LRU map. */
#include <glib.h>

#include "tickmap.h"

static GPtrArray *freed;

static void
record_free(gpointer data)
{
	g_ptr_array_add(freed, data);
}

static void
test_states(void)
{
	g_assert_cmpint(x2_tick_state_from_marker("received"), ==,
	                X2_TICK_DELIVERED);
	g_assert_cmpint(x2_tick_state_from_marker("displayed"), ==,
	                X2_TICK_DISPLAYED);
	g_assert_cmpint(x2_tick_state_from_marker("acknowledged"), ==,
	                X2_TICK_DISPLAYED);
	g_assert_cmpint(x2_tick_state_from_marker("bogus"), ==, X2_TICK_NONE);
	g_assert_cmpint(x2_tick_state_from_marker(NULL), ==, X2_TICK_NONE);

	/* a late receipt never takes back a read marker */
	g_assert_cmpint(x2_tick_state_merge(X2_TICK_DISPLAYED, X2_TICK_DELIVERED),
	                ==, X2_TICK_DISPLAYED);
	g_assert_cmpint(x2_tick_state_merge(X2_TICK_NONE, X2_TICK_DELIVERED),
	                ==, X2_TICK_DELIVERED);
}

static void
test_glyphs(void)
{
	g_assert_null(x2_tick_glyph(X2_TICK_STYLE_CHECKS, X2_TICK_NONE));
	g_assert_cmpstr(x2_tick_glyph(X2_TICK_STYLE_CHECKS, X2_TICK_DELIVERED),
	                ==, "\xe2\x9c\x93");
	g_assert_cmpstr(x2_tick_glyph(X2_TICK_STYLE_CHECKS, X2_TICK_DISPLAYED),
	                ==, "\xe2\x9c\x93\xe2\x9c\x93");
	g_assert_cmpstr(x2_tick_glyph(X2_TICK_STYLE_ASCII, X2_TICK_DISPLAYED),
	                ==, "++");
	g_assert_cmpstr(x2_tick_glyph(X2_TICK_STYLE_WORDS, X2_TICK_DELIVERED),
	                ==, "(delivered)");

	g_assert_cmpint(x2_tick_style_from_string("ascii"), ==,
	                X2_TICK_STYLE_ASCII);
	g_assert_cmpint(x2_tick_style_from_string("words"), ==,
	                X2_TICK_STYLE_WORDS);
	g_assert_cmpint(x2_tick_style_from_string("checks"), ==,
	                X2_TICK_STYLE_CHECKS);
	g_assert_cmpint(x2_tick_style_from_string(NULL), ==,
	                X2_TICK_STYLE_CHECKS);
	g_assert_cmpint(x2_tick_style_from_string("emoji"), ==,
	                X2_TICK_STYLE_CHECKS);
}

static void
test_insert_lookup(void)
{
	X2TickMap *map = x2_tick_map_new(4, g_free);

	x2_tick_map_insert(map, "a", g_strdup("A"));
	x2_tick_map_insert(map, "b", g_strdup("B"));
	g_assert_cmpuint(x2_tick_map_size(map), ==, 2);
	g_assert_cmpstr(x2_tick_map_lookup(map, "a"), ==, "A");
	g_assert_null(x2_tick_map_lookup(map, "c"));
	g_assert_null(x2_tick_map_lookup(map, NULL));

	/* same id again: the old value is freed, the size stays */
	x2_tick_map_insert(map, "a", g_strdup("A2"));
	g_assert_cmpuint(x2_tick_map_size(map), ==, 2);
	g_assert_cmpstr(x2_tick_map_lookup(map, "a"), ==, "A2");

	g_assert_true(x2_tick_map_remove(map, "a"));
	g_assert_false(x2_tick_map_remove(map, "a"));
	g_assert_cmpuint(x2_tick_map_size(map), ==, 1);
	x2_tick_map_free(map);
	x2_tick_map_free(NULL);
}

static void
test_lru_eviction(void)
{
	X2TickMap *map;

	freed = g_ptr_array_new();
	map = x2_tick_map_new(3, record_free);
	x2_tick_map_insert(map, "a", "A");
	x2_tick_map_insert(map, "b", "B");
	x2_tick_map_insert(map, "c", "C");
	/* touching a makes b the least recently used */
	g_assert_nonnull(x2_tick_map_lookup(map, "a"));
	x2_tick_map_insert(map, "d", "D");

	g_assert_cmpuint(x2_tick_map_size(map), ==, 3);
	g_assert_null(x2_tick_map_lookup(map, "b"));
	g_assert_nonnull(x2_tick_map_lookup(map, "a"));
	g_assert_cmpuint(freed->len, ==, 1);
	g_assert_cmpstr(g_ptr_array_index(freed, 0), ==, "B");

	x2_tick_map_free(map);
	g_assert_cmpuint(freed->len, ==, 4);
	g_ptr_array_free(freed, TRUE);
	freed = NULL;
}

static void
test_many_ids_stay_bounded(void)
{
	X2TickMap *map = x2_tick_map_new(200, g_free);
	guint i;

	for (i = 0; i < 10000; i++) {
		char *id = g_strdup_printf("id-%u", i);

		x2_tick_map_insert(map, id, g_strdup(id));
		g_free(id);
	}
	g_assert_cmpuint(x2_tick_map_size(map), ==, 200);
	g_assert_null(x2_tick_map_lookup(map, "id-9799"));
	g_assert_cmpstr(x2_tick_map_lookup(map, "id-9800"), ==, "id-9800");
	g_assert_cmpstr(x2_tick_map_lookup(map, "id-9999"), ==, "id-9999");
	x2_tick_map_free(map);
}

static gboolean
is_even(gpointer key, gpointer value, gpointer data)
{
	guint *seen = data;

	(*seen)++;
	return GPOINTER_TO_UINT(value) % 2 == 0;
}

static void
test_foreach_remove(void)
{
	X2TickMap *map = x2_tick_map_new(10, NULL);
	guint seen = 0;

	x2_tick_map_insert(map, "1", GUINT_TO_POINTER(1));
	x2_tick_map_insert(map, "2", GUINT_TO_POINTER(2));
	x2_tick_map_insert(map, "3", GUINT_TO_POINTER(3));
	x2_tick_map_insert(map, "4", GUINT_TO_POINTER(4));
	g_assert_cmpuint(x2_tick_map_foreach_remove(map, is_even, &seen), ==, 2);
	g_assert_cmpuint(seen, ==, 4);
	g_assert_cmpuint(x2_tick_map_size(map), ==, 2);
	g_assert_null(x2_tick_map_lookup(map, "2"));
	g_assert_nonnull(x2_tick_map_lookup(map, "3"));

	x2_tick_map_clear(map);
	g_assert_cmpuint(x2_tick_map_size(map), ==, 0);
	/* still usable after a clear */
	x2_tick_map_insert(map, "5", GUINT_TO_POINTER(5));
	g_assert_cmpuint(x2_tick_map_size(map), ==, 1);
	x2_tick_map_free(map);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/tickmap/states", test_states);
	g_test_add_func("/tickmap/glyphs", test_glyphs);
	g_test_add_func("/tickmap/insert-lookup", test_insert_lookup);
	g_test_add_func("/tickmap/lru-eviction", test_lru_eviction);
	g_test_add_func("/tickmap/many-ids-bounded", test_many_ids_stay_bounded);
	g_test_add_func("/tickmap/foreach-remove", test_foreach_remove);
	return g_test_run();
}
