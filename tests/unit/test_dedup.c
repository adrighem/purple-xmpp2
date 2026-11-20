/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/* Unit tests of core/dedup.c: LRU set and key building. */
#include <glib.h>

#include "dedup.h"

static void
test_add_and_contains(void)
{
	X2Dedup *d = x2_dedup_new(4);

	g_assert_cmpuint(x2_dedup_size(d), ==, 0);
	g_assert_false(x2_dedup_contains(d, "a"));
	g_assert_true(x2_dedup_add(d, "a"));
	g_assert_true(x2_dedup_contains(d, "a"));
	g_assert_false(x2_dedup_add(d, "a"));
	g_assert_cmpuint(x2_dedup_size(d), ==, 1);
	x2_dedup_free(d);
}

static void
test_eviction_is_lru(void)
{
	X2Dedup *d = x2_dedup_new(3);

	x2_dedup_add(d, "a");
	x2_dedup_add(d, "b");
	x2_dedup_add(d, "c");
	/* touching "a" makes "b" the least recently used */
	g_assert_false(x2_dedup_add(d, "a"));
	g_assert_true(x2_dedup_add(d, "d"));

	g_assert_cmpuint(x2_dedup_size(d), ==, 3);
	g_assert_false(x2_dedup_contains(d, "b"));
	g_assert_true(x2_dedup_contains(d, "a"));
	g_assert_true(x2_dedup_contains(d, "c"));
	g_assert_true(x2_dedup_contains(d, "d"));
	x2_dedup_free(d);
}

static void
test_contains_does_not_touch(void)
{
	X2Dedup *d = x2_dedup_new(2);

	x2_dedup_add(d, "a");
	x2_dedup_add(d, "b");
	g_assert_true(x2_dedup_contains(d, "a"));
	x2_dedup_add(d, "c");
	/* "a" was still the oldest, contains() must not have refreshed it */
	g_assert_false(x2_dedup_contains(d, "a"));
	g_assert_true(x2_dedup_contains(d, "b"));
	x2_dedup_free(d);
}

static void
test_capacity_one_and_many(void)
{
	X2Dedup *d = x2_dedup_new(1);
	X2Dedup *big = x2_dedup_new(100);
	char key[16];
	int i;

	x2_dedup_add(d, "a");
	x2_dedup_add(d, "b");
	g_assert_false(x2_dedup_contains(d, "a"));
	g_assert_true(x2_dedup_contains(d, "b"));
	x2_dedup_free(d);

	for (i = 0; i < 1000; i++) {
		g_snprintf(key, sizeof(key), "k%d", i);
		g_assert_true(x2_dedup_add(big, key));
	}
	g_assert_cmpuint(x2_dedup_size(big), ==, 100);
	g_assert_false(x2_dedup_contains(big, "k899"));
	g_assert_true(x2_dedup_contains(big, "k900"));
	g_assert_true(x2_dedup_contains(big, "k999"));
	x2_dedup_free(big);
}

static void
test_free_null(void)
{
	x2_dedup_free(NULL);
}

static void
check_key(const char *sid, const char *archive, gboolean out,
          const char *peer, const char *oid, const char *expected)
{
	char *key = x2_dedup_make_key(sid, archive, out, peer, oid);

	g_assert_cmpstr(key, ==, expected);
	g_free(key);
}

static void
test_make_key(void)
{
	/* trusted stanza-id wins over the origin-id */
	check_key("sid1", "me@x", FALSE, "bob@x", "o1", "S:me@x:sid1");
	/* direction and peer are part of an origin-id key */
	check_key(NULL, NULL, FALSE, "bob@x", "o1", "O:in:bob@x:o1");
	check_key(NULL, NULL, TRUE, "bob@x", "o1", "O:out:bob@x:o1");
	check_key("", "me@x", TRUE, "room@c/nick", "o1",
	          "O:out:room@c/nick:o1");
	/* nothing usable: the stanza id attribute is never a key */
	check_key(NULL, NULL, FALSE, "bob@x", NULL, NULL);
	check_key("", "me@x", TRUE, "bob@x", "", NULL);
	check_key(NULL, NULL, FALSE, NULL, "o1", NULL);
	check_key(NULL, NULL, FALSE, "", "o1", NULL);
	/* a stanza-id needs no peer */
	check_key("sid1", "me@x", FALSE, NULL, NULL, "S:me@x:sid1");
}

static void
test_stanza_id_keys_are_per_archive(void)
{
	char *a = x2_dedup_make_key("42", "me@x", FALSE, "bob@x", NULL);
	char *b = x2_dedup_make_key("42", "room@c", FALSE, "room@c", NULL);
	char *c = x2_dedup_make_key("42", "me@x", TRUE, "carol@x", NULL);

	/* the same id from two archives is two messages ... */
	g_assert_cmpstr(a, !=, b);
	/* ... and from one archive it is one, whoever the peer is */
	g_assert_cmpstr(a, ==, c);
	g_free(a);
	g_free(b);
	g_free(c);
}

static void
test_stanza_id_without_archive_is_unusable(void)
{
	char *key;

	/* no archive to scope it: fall back to the origin-id, or no key */
	key = x2_dedup_make_key("42", NULL, FALSE, "bob@x", "o1");
	g_assert_cmpstr(key, ==, "O:in:bob@x:o1");
	g_free(key);
	key = x2_dedup_make_key("42", "", FALSE, "bob@x", NULL);
	g_assert_null(key);
}

static void
test_old_format_keys_age_out(void)
{
	X2Dedup *d = x2_dedup_new(2);
	char *key = x2_dedup_make_key("42", "me@x", FALSE, "bob@x", NULL);

	/* a key from before the archive was part of it (a ring from an old
	 * state file) is just another string: it never matches a new key and
	 * is evicted like any other */
	x2_dedup_add(d, "S:42");
	g_assert_false(x2_dedup_contains(d, key));
	x2_dedup_add(d, key);
	x2_dedup_add(d, "O:in:bob@x:o9");
	g_assert_false(x2_dedup_contains(d, "S:42"));
	g_assert_true(x2_dedup_contains(d, key));
	g_free(key);
	x2_dedup_free(d);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/dedup/add_and_contains", test_add_and_contains);
	g_test_add_func("/dedup/eviction_is_lru", test_eviction_is_lru);
	g_test_add_func("/dedup/contains_does_not_touch",
	                test_contains_does_not_touch);
	g_test_add_func("/dedup/capacity", test_capacity_one_and_many);
	g_test_add_func("/dedup/free_null", test_free_null);
	g_test_add_func("/dedup/make_key", test_make_key);
	g_test_add_func("/dedup/key_per_archive",
	                test_stanza_id_keys_are_per_archive);
	g_test_add_func("/dedup/key_no_archive",
	                test_stanza_id_without_archive_is_unusable);
	g_test_add_func("/dedup/old_keys_age_out", test_old_format_keys_age_out);
	return g_test_run();
}
