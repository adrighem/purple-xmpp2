/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/*
 * Unit tests of core/store.c: round-trip, ring bound, atomic write and
 * corrupt-file handling, all in a temporary directory.
 */
#include <sys/stat.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "x2test.h"

#include "store.h"

#pragma GCC diagnostic ignored "-Wunused-parameter"  /* GTest fixture signatures */

#define ME "alice@example.org"

typedef struct {
	char *dir;
	char *path;
} Fixture;

static void
setup(Fixture *f, gconstpointer data)
{
	f->dir = g_dir_make_tmp("x2store-XXXXXX", NULL);
	g_assert_nonnull(f->dir);
	f->path = g_build_filename(f->dir, "a.state", NULL);
}

static void
teardown(Fixture *f, gconstpointer data)
{
	g_unlink(f->path);
	g_rmdir(f->dir);
	g_free(f->path);
	g_free(f->dir);
}

static void
collect(gpointer key, gpointer data)
{
	g_ptr_array_add(data, g_strdup(key));
}

static void
test_missing_file(Fixture *f, gconstpointer data)
{
	X2Store *s = x2_store_load(f->path);

	g_assert_null(x2_store_get_cursor(s, ME, NULL));
	g_assert_false(x2_store_cursor_only(s));
	g_assert_false(x2_store_is_dirty(s));
	x2_store_free(s);
}

static void
test_round_trip(Fixture *f, gconstpointer data)
{
	X2Store *s = x2_store_new();
	GError *err = NULL;
	struct stat st;
	gint64 ts;

	x2_store_set_cursor(s, ME, "id-1", 1763540000);
	x2_store_ring_add(s, ME, "O:out:bob@x:u1");
	x2_store_ring_add(s, ME, "O:in:carol@x:semi;colon");
	x2_store_set_cursor_only(s, TRUE);
	g_assert_true(x2_store_is_dirty(s));
	g_assert_true(x2_store_save(s, f->path, &err));
	g_assert_no_error(err);
	g_assert_false(x2_store_is_dirty(s));
	x2_store_free(s);

	g_assert_cmpint(g_stat(f->path, &st), ==, 0);
	g_assert_cmpint(st.st_mode & 0777, ==, 0600);

	s = x2_store_load(f->path);
	g_assert_cmpstr(x2_store_get_cursor(s, ME, &ts), ==, "id-1");
	g_assert_cmpint(ts, ==, 1763540000);
	g_assert_true(x2_store_ring_contains(s, ME, "O:out:bob@x:u1"));
	g_assert_true(x2_store_ring_contains(s, ME, "O:in:carol@x:semi;colon"));
	g_assert_false(x2_store_ring_contains(s, "other@x", "O:out:bob@x:u1"));
	g_assert_true(x2_store_cursor_only(s));
	g_assert_false(x2_store_is_dirty(s));
	x2_store_free(s);
}

static void
test_cursor_keeps_ts(Fixture *f, gconstpointer data)
{
	X2Store *s = x2_store_new();
	gint64 ts;

	x2_store_set_cursor(s, ME, "a", 100);
	x2_store_set_cursor(s, ME, "b", 0);
	g_assert_cmpstr(x2_store_get_cursor(s, ME, &ts), ==, "b");
	g_assert_cmpint(ts, ==, 100);
	x2_store_free(s);
}

static void
test_ring_bounded(Fixture *f, gconstpointer data)
{
	X2Store *s = x2_store_new();
	GPtrArray *keys = g_ptr_array_new_with_free_func(g_free);
	int i;

	for (i = 0; i < X2_STORE_RING_MAX + 10; i++) {
		char *k = g_strdup_printf("O:in:bob@x:%d", i);

		x2_store_ring_add(s, ME, k);
		g_free(k);
	}
	/* adding a key twice keeps one copy */
	x2_store_ring_add(s, ME, "O:in:bob@x:265");

	g_assert_false(x2_store_ring_contains(s, ME, "O:in:bob@x:0"));
	g_assert_false(x2_store_ring_contains(s, ME, "O:in:bob@x:9"));
	g_assert_true(x2_store_ring_contains(s, ME, "O:in:bob@x:10"));
	x2_store_ring_foreach(s, collect, keys);
	g_assert_cmpuint(keys->len, ==, X2_STORE_RING_MAX);
	g_assert_cmpstr(g_ptr_array_index(keys, 0), ==, "O:in:bob@x:10");

	g_assert_true(x2_store_save(s, f->path, NULL));
	x2_store_free(s);
	s = x2_store_load(f->path);
	g_ptr_array_set_size(keys, 0);
	x2_store_ring_foreach(s, collect, keys);
	g_assert_cmpuint(keys->len, ==, X2_STORE_RING_MAX);
	g_ptr_array_unref(keys);
	x2_store_free(s);
}

static void
test_corrupt_file(Fixture *f, gconstpointer data)
{
	X2Store *s;

	g_assert_true(g_file_set_contents(f->path, "[archive x\ncursor=\x01",
	                                  -1, NULL));
	g_test_expect_message(G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
	                      "*unreadable*");
	s = x2_store_load(f->path);
	g_test_assert_expected_messages();
	g_assert_null(x2_store_get_cursor(s, "x", NULL));
	g_assert_false(x2_store_is_dirty(s));
	x2_store_free(s);
}

static void
test_atomic_overwrite(Fixture *f, gconstpointer data)
{
	X2Store *s = x2_store_new();
	GDir *dir;
	const char *name;
	int n = 0;

	x2_store_set_cursor(s, ME, "a", 1);
	g_assert_true(x2_store_save(s, f->path, NULL));
	x2_store_set_cursor(s, ME, "b", 2);
	g_assert_true(x2_store_save(s, f->path, NULL));
	x2_store_free(s);

	/* temp file renamed over the target: nothing else left behind */
	dir = g_dir_open(f->dir, 0, NULL);
	while ((name = g_dir_read_name(dir)) != NULL)
		n++;
	g_dir_close(dir);
	g_assert_cmpint(n, ==, 1);

	s = x2_store_load(f->path);
	g_assert_cmpstr(x2_store_get_cursor(s, ME, NULL), ==, "b");
	x2_store_free(s);
}

static void
test_save_fails_loudly(Fixture *f, gconstpointer data)
{
	X2Store *s = x2_store_new();
	GError *err = NULL;
	char *bad = g_build_filename(f->dir, "missing", "a.state", NULL);

	x2_store_set_cursor(s, ME, "a", 1);
	g_assert_false(x2_store_save(s, bad, &err));
	g_assert_nonnull(err);
	g_assert_true(x2_store_is_dirty(s));
	g_error_free(err);
	g_free(bad);
	x2_store_free(s);
}

#define ADD(name, fn) \
	g_test_add(name, Fixture, NULL, setup, fn, teardown)

int
main(int argc, char **argv)
{
	x2_test_init(&argc, &argv);

	ADD("/store/missing", test_missing_file);
	ADD("/store/round-trip", test_round_trip);
	ADD("/store/cursor-keeps-ts", test_cursor_keeps_ts);
	ADD("/store/ring-bounded", test_ring_bounded);
	ADD("/store/corrupt", test_corrupt_file);
	ADD("/store/atomic", test_atomic_overwrite);
	ADD("/store/save-fails", test_save_fails_loudly);
	return g_test_run();
}
