/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/* Unit tests of core/migrate_map.c: option mapping and username equality. */
#include <glib.h>

#include "migrate_map.h"

/* libpurple 2 has no constructor for the pairs; the option owns the list */
static GList *
sec_values(void)
{
	static const char *const v[][2] = {
		{ "Require", "require_tls" }, { "Maybe", "opportunistic_tls" }
	};
	GList *l = NULL;
	guint i;

	for (i = 0; i < G_N_ELEMENTS(v); i++) {
		PurpleKeyValuePair *kv = g_new0(PurpleKeyValuePair, 1);

		kv->key = g_strdup(v[i][0]);
		kv->value = g_strdup(v[i][1]);
		l = g_list_append(l, kv);
	}
	return l;
}

static GList *
src_options(void)
{
	GList *o = NULL;

	o = g_list_append(o, purple_account_option_list_new("Security",
	        "connection_security", sec_values()));
	o = g_list_append(o, purple_account_option_bool_new("Plain",
	        "auth_plain_in_clear", FALSE));
	o = g_list_append(o, purple_account_option_int_new("Port", "port", 5222));
	o = g_list_append(o, purple_account_option_string_new("Server",
	        "connect_server", NULL));
	o = g_list_append(o, purple_account_option_string_new("Old only",
	        "old_only", NULL));
	o = g_list_append(o, purple_account_option_string_new("Typed",
	        "typed", NULL));
	return o;
}

static GList *
dst_options(void)
{
	GList *o = NULL;

	o = g_list_append(o, purple_account_option_string_new("Server",
	        "connect_server", NULL));
	o = g_list_append(o, purple_account_option_int_new("Port", "port", 5222));
	o = g_list_append(o, purple_account_option_list_new("Security",
	        "connection_security", sec_values()));
	o = g_list_append(o, purple_account_option_bool_new("Plain",
	        "auth_plain_in_clear", FALSE));
	o = g_list_append(o, purple_account_option_bool_new("New only",
	        "new_only", FALSE));
	/* same key as in src, but another type */
	o = g_list_append(o, purple_account_option_int_new("Typed", "typed", 0));
	return o;
}

static void
free_options(GList *o)
{
	g_list_free_full(o, (GDestroyNotify)purple_account_option_destroy);
}

static void
test_map_common_options(void)
{
	GList *src = src_options(), *dst = dst_options(), *m, *l;
	const char *want[] = { "connection_security", "auth_plain_in_clear",
	                       "port", "connect_server" };
	X2MigrateKind kinds[] = { X2_MIGRATE_STRING, X2_MIGRATE_BOOL,
	                          X2_MIGRATE_INT, X2_MIGRATE_STRING };
	guint i = 0;

	m = x2_migrate_map_options(src, dst);
	g_assert_cmpuint(g_list_length(m), ==, G_N_ELEMENTS(want));
	for (l = m; l != NULL; l = l->next, i++) {
		X2MigrateOpt *o = l->data;

		/* order of the source prpl, key and kind of the target */
		g_assert_cmpstr(o->name, ==, want[i]);
		g_assert_cmpint(o->kind, ==, kinds[i]);
		g_assert_cmpstr(purple_account_option_get_setting(o->dst), ==,
		                want[i]);
	}
	x2_migrate_opts_free(m);
	free_options(src);
	free_options(dst);
}

static void
test_map_empty_sides(void)
{
	GList *src = src_options(), *m;

	m = x2_migrate_map_options(src, NULL);
	g_assert_null(m);
	m = x2_migrate_map_options(NULL, src);
	g_assert_null(m);
	free_options(src);
}

static void
test_list_values_are_checked(void)
{
	GList *src = src_options(), *dst = dst_options(), *m, *l;

	m = x2_migrate_map_options(src, dst);
	for (l = m; l != NULL; l = l->next) {
		X2MigrateOpt *o = l->data;

		if (g_strcmp0(o->name, "connection_security") == 0) {
			g_assert_true(x2_migrate_value_allowed(o, "require_tls"));
			g_assert_true(x2_migrate_value_allowed(o, "opportunistic_tls"));
			g_assert_false(x2_migrate_value_allowed(o, "old_ssl"));
			g_assert_false(x2_migrate_value_allowed(o, ""));
		} else {
			/* no list: anything goes */
			g_assert_true(x2_migrate_value_allowed(o, "whatever"));
		}
	}
	x2_migrate_opts_free(m);
	free_options(src);
	free_options(dst);
}

static void
test_same_username(void)
{
	g_assert_true(x2_migrate_same_username("a@x.org/home", "a@x.org/home"));
	g_assert_true(x2_migrate_same_username("A@X.org/home", "a@x.ORG/home"));
	g_assert_true(x2_migrate_same_username("a@x.org", "a@x.org/"));
	g_assert_true(x2_migrate_same_username("a@x.org", "a@x.org"));
	/* the resource is ignored: same bare JID, same account */
	g_assert_true(x2_migrate_same_username("a@x.org/Home", "a@x.org/home"));
	g_assert_true(x2_migrate_same_username("a@x.org/home", "a@x.org"));
	g_assert_true(x2_migrate_same_username("a@x.org/h", "a@x.org/h/x"));
	g_assert_true(x2_migrate_same_username("A@x.org/laptop", "a@X.ORG/phone"));
	g_assert_false(x2_migrate_same_username("a@x.org", "b@x.org"));
	g_assert_false(x2_migrate_same_username("a@x.org", "a@x.org.evil"));
	g_assert_false(x2_migrate_same_username("a@x.org", "ba@x.org"));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/migrate/map/common", test_map_common_options);
	g_test_add_func("/migrate/map/empty", test_map_empty_sides);
	g_test_add_func("/migrate/map/list-values", test_list_values_are_checked);
	g_test_add_func("/migrate/username", test_same_username);
	return g_test_run();
}
