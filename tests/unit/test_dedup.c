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
check_key(const char *sid, gboolean out, const char *peer, const char *oid,
          const char *id, const char *expected)
{
	char *key = x2_dedup_make_key(sid, out, peer, oid, id);

	g_assert_cmpstr(key, ==, expected);
	g_free(key);
}

static void
test_make_key(void)
{
	/* trusted stanza-id wins over everything */
	check_key("sid1", FALSE, "bob@x", "o1", "i1", "S:sid1");
	/* origin-id before the id attribute, direction and peer are part of it */
	check_key(NULL, FALSE, "bob@x", "o1", "i1", "O:in:bob@x:o1");
	check_key(NULL, TRUE, "bob@x", "o1", "i1", "O:out:bob@x:o1");
	check_key("", TRUE, "bob@x", "", "i1", "I:out:bob@x:i1");
	check_key(NULL, FALSE, "room@c/nick", NULL, "i1", "I:in:room@c/nick:i1");
	/* nothing usable */
	check_key(NULL, FALSE, "bob@x", NULL, NULL, NULL);
	check_key(NULL, FALSE, "bob@x", "", "", NULL);
	check_key(NULL, FALSE, NULL, "o1", "i1", NULL);
	/* a stanza-id needs no peer */
	check_key("sid1", FALSE, NULL, NULL, NULL, "S:sid1");
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
	return g_test_run();
}
