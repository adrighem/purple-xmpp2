/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file migrate_map.h Pure helpers of the prpl-jabber account migration
 *
 * Which account options of the original prpl may be copied to the clone
 * (ARCHITECTURE 10), and when two usernames name the same account.  Only
 * PurpleAccountOption descriptions and strings are used, so this is unit
 * tested without a libpurple core.
 */
#ifndef XMPP2_CORE_MIGRATE_MAP_H_
#define XMPP2_CORE_MIGRATE_MAP_H_

#include <glib.h>
#include "accountopt.h"
#include "util.h"

/** How a setting is stored in an account (libpurple has three getters). */
typedef enum {
	X2_MIGRATE_STRING,  /* string options and list options */
	X2_MIGRATE_INT,
	X2_MIGRATE_BOOL
} X2MigrateKind;

typedef struct {
	char *name;                      /* the setting key, equal on both sides */
	X2MigrateKind kind;
	const PurpleAccountOption *dst;  /* the target option, owned by its list */
} X2MigrateOpt;

/**
 * The options that exist in both lists under the same key and with a
 * compatible type.  Types other than string, list, int and bool are never
 * copied.
 *
 * @param src  protocol_options of the original prpl.
 * @param dst  protocol_options of the target prpl; must outlive the result.
 * @return A list of X2MigrateOpt in the order of @a src; free with
 *         x2_migrate_opts_free().
 */
GList *x2_migrate_map_options(GList *src, GList *dst);

void x2_migrate_opts_free(GList *opts);

/**
 * TRUE if @a value may be stored for the target option: always, unless the
 * option is a list option, which only accepts the values it offers.
 */
gboolean x2_migrate_value_allowed(const X2MigrateOpt *opt, const char *value);

/**
 * TRUE if two account usernames name the same XMPP account: the bare JID
 * compares case-insensitively, the resource exactly, and an empty resource
 * equals none.
 */
gboolean x2_migrate_same_username(const char *a, const char *b);

#endif /* XMPP2_CORE_MIGRATE_MAP_H_ */
