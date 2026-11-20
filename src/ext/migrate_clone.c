/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file migrate_clone.c Copy of the settings of one account
 *
 * Passwords and proxy credentials are copied but never logged.
 */
#include "xmpp2_internal.h"

#include <limits.h>

#include "buddyicon.h"
#include "debug.h"
#include "imgstore.h"
#include "proxy.h"

#include "core/migrate_map.h"
#include "ext/migrate.h"
#include "ext/statefile.h"

#define DBG X2_MIGRATE_DEBUG

/*
 * libpurple has no "is this setting set" query, but a getter returns its
 * default for a missing key: two different defaults that come back equal
 * mean the value is stored.
 */
static void
copy_option(PurpleAccount *orig, PurpleAccount *dst, const X2MigrateOpt *opt)
{
	switch (opt->kind) {
	case X2_MIGRATE_STRING: {
		const char *v = purple_account_get_string(orig, opt->name, NULL);

		if (v == NULL)
			return;
		if (!x2_migrate_value_allowed(opt, v)) {
			purple_debug_warning(DBG, "option %s: value not offered "
			                     "by xmpp2, left at its default\n",
			                     opt->name);
			return;
		}
		purple_account_set_string(dst, opt->name, v);
		break;
	}
	case X2_MIGRATE_INT: {
		int a = purple_account_get_int(orig, opt->name, INT_MIN);
		int b = purple_account_get_int(orig, opt->name, INT_MIN + 1);

		if (a == b)
			purple_account_set_int(dst, opt->name, a);
		break;
	}
	case X2_MIGRATE_BOOL: {
		gboolean a = purple_account_get_bool(orig, opt->name, FALSE);
		gboolean b = purple_account_get_bool(orig, opt->name, TRUE);

		if (a == b)
			purple_account_set_bool(dst, opt->name, a);
		break;
	}
	}
}

static void
copy_proxy(PurpleAccount *orig, PurpleAccount *dst)
{
	PurpleProxyInfo *from = purple_account_get_proxy_info(orig), *to;

	if (from == NULL)
		return;
	to = purple_proxy_info_new();
	purple_proxy_info_set_type(to, purple_proxy_info_get_type(from));
	purple_proxy_info_set_host(to, purple_proxy_info_get_host(from));
	purple_proxy_info_set_port(to, purple_proxy_info_get_port(from));
	purple_proxy_info_set_username(to, purple_proxy_info_get_username(from));
	purple_proxy_info_set_password(to, purple_proxy_info_get_password(from));
	purple_account_set_proxy_info(dst, to);
}

/*
 * The avatar libpurple has cached for the account (what we publish): a copy
 * of the bytes, set_account_icon() keeps the buffer.  The user chosen
 * buddy_icon_path is copied by copy_profile().
 */
static void
copy_account_icon(PurpleAccount *orig, PurpleAccount *dst)
{
	PurpleStoredImage *img = purple_buddy_icons_find_account_icon(orig);
	gsize len;
	guchar *copy;

	if (img == NULL)
		return;
	len = purple_imgstore_get_size(img);
	if (len > 0) {
		copy = g_malloc(len);
		memcpy(copy, purple_imgstore_get_data(img), len);
		purple_buddy_icons_set_account_icon(dst, copy, len);
	}
	purple_imgstore_unref(img);
}

/* alias, user info, buddy icon and the other account level settings */
static void
copy_profile(PurpleAccount *orig, PurpleAccount *dst)
{
	const char *s;
	gboolean a, b;

	if ((s = purple_account_get_alias(orig)) != NULL)
		purple_account_set_alias(dst, s);
	if ((s = purple_account_get_user_info(orig)) != NULL)
		purple_account_set_user_info(dst, s);
	if ((s = purple_account_get_buddy_icon_path(orig)) != NULL)
		purple_account_set_buddy_icon_path(dst, s);
	purple_account_set_check_mail(dst, purple_account_get_check_mail(orig));

	a = purple_account_get_bool(orig, "use-global-buddyicon", FALSE);
	b = purple_account_get_bool(orig, "use-global-buddyicon", TRUE);
	if (a == b)
		purple_account_set_bool(dst, "use-global-buddyicon", a);
}

/* The first catch-up must only set the cursor (ARCHITECTURE 7.2, 9). */
static void
seed_state(PurpleAccount *dst)
{
	X2StateFile *sf = x2_statefile_open(dst);

	x2_store_set_cursor_only(x2_statefile_store(sf), TRUE);
	x2_statefile_flush(sf);
	x2_statefile_close(sf);
}

PurpleAccount *
x2_migrate_clone_account(PurpleAccount *orig, GList *opts)
{
	PurpleAccount *dst;
	const char *username = purple_account_get_username(orig);
	GList *l;

	g_return_val_if_fail(orig != NULL, NULL);

	dst = purple_account_new(username, XMPP2_PRPL_ID);

	/* only a saved password is copied */
	if (purple_account_get_remember_password(orig) &&
	    purple_account_get_password(orig) != NULL) {
		purple_account_set_remember_password(dst, TRUE);
		purple_account_set_password(dst, purple_account_get_password(orig));
	}
	for (l = opts; l != NULL; l = l->next)
		copy_option(orig, dst, l->data);
	copy_proxy(orig, dst);
	copy_profile(orig, dst);
	copy_account_icon(orig, dst);

	purple_accounts_add(dst);
	seed_state(dst);
	purple_debug_info(DBG, "cloned %s (password %scopied)\n", username,
	                  purple_account_get_remember_password(dst) ? "" : "not ");
	return dst;
}
