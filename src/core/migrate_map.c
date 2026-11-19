/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file migrate_map.c Pure helpers of the prpl-jabber account migration
 */
#include <string.h>

#include "util.h"

#include "migrate_map.h"
#include "jid.h"

static gboolean
kind_of(PurplePrefType type, X2MigrateKind *kind)
{
	switch (type) {
	case PURPLE_PREF_STRING:
	case PURPLE_PREF_STRING_LIST:
		*kind = X2_MIGRATE_STRING;
		return TRUE;
	case PURPLE_PREF_INT:
		*kind = X2_MIGRATE_INT;
		return TRUE;
	case PURPLE_PREF_BOOLEAN:
		*kind = X2_MIGRATE_BOOL;
		return TRUE;
	default:
		return FALSE;
	}
}

static const PurpleAccountOption *
find_option(GList *opts, const char *name)
{
	GList *l;

	for (l = opts; l != NULL; l = l->next) {
		const char *key = purple_account_option_get_setting(l->data);

		if (key != NULL && strcmp(key, name) == 0)
			return l->data;
	}
	return NULL;
}

GList *
x2_migrate_map_options(GList *src, GList *dst)
{
	GList *out = NULL, *l;

	for (l = src; l != NULL; l = l->next) {
		const char *name = purple_account_option_get_setting(l->data);
		const PurpleAccountOption *target;
		X2MigrateKind from, to;
		X2MigrateOpt *opt;

		if (name == NULL ||
		    !kind_of(purple_account_option_get_type(l->data), &from))
			continue;
		target = find_option(dst, name);
		if (target == NULL ||
		    !kind_of(purple_account_option_get_type(target), &to) ||
		    from != to)
			continue;

		opt = g_new0(X2MigrateOpt, 1);
		opt->name = g_strdup(name);
		opt->kind = to;
		opt->dst = target;
		out = g_list_append(out, opt);
	}
	return out;
}

void
x2_migrate_opts_free(GList *opts)
{
	GList *l;

	for (l = opts; l != NULL; l = l->next) {
		X2MigrateOpt *opt = l->data;

		g_free(opt->name);
		g_free(opt);
	}
	g_list_free(opts);
}

gboolean
x2_migrate_value_allowed(const X2MigrateOpt *opt, const char *value)
{
	GList *l;

	g_return_val_if_fail(opt != NULL, FALSE);
	g_return_val_if_fail(value != NULL, FALSE);

	if (purple_account_option_get_type(opt->dst) != PURPLE_PREF_STRING_LIST)
		return TRUE;
	for (l = purple_account_option_get_list(opt->dst); l != NULL;
	     l = l->next) {
		const PurpleKeyValuePair *kv = l->data;

		if (kv->value != NULL && strcmp(kv->value, value) == 0)
			return TRUE;
	}
	return FALSE;
}

gboolean
x2_migrate_same_username(const char *a, const char *b)
{
	g_return_val_if_fail(a != NULL && b != NULL, FALSE);

	return x2_jid_same_bare(a, b);
}
