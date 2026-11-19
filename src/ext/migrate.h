/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file migrate.h Copy of prpl-jabber accounts to prpl-xmpp2 (ARCHITECTURE 10)
 *
 * migrate.c   the module: startup offer, the prpl and plugin actions, the
 *             loop over the originals
 * migrate_clone.c  one account: settings, password, proxy, archive state
 * migrate_blist.c  the buddy list entries of one account
 *
 * The originals are never deleted; the caller disables them.
 */
#ifndef XMPP2_EXT_MIGRATE_H_
#define XMPP2_EXT_MIGRATE_H_

#include <glib.h>
#include "account.h"
#include "plugin.h"

#include "ext/ext.h"

#define X2_MIGRATE_DEBUG "xmpp2-migrate"
#define X2_MIGRATE_ORIGINAL_PRPL "prpl-jabber"

/* "Never" was chosen in the startup offer */
#define X2_MIGRATE_PREF_NEVER "/plugins/prpl/xmpp2/migration_never"
/* Id of the invisible plugin that shows the action under Tools */
#define X2_MIGRATE_ACTIONS_PLUGIN_ID "core-xmpp2-migrate"

#ifdef XMPP2_TEST_HOOKS
/* Test hook: also offer in UIs other than Pidgin (the harness has none) */
#define X2_MIGRATE_PREF_ANY_UI "/plugins/prpl/xmpp2/migration_offer_any_ui"
#endif

extern const X2Ext x2_ext_migrate;

/** Number of prpl-jabber accounts that have no prpl-xmpp2 twin. */
guint x2_migrate_pending(void);

/** Appends the "Copy XMPP accounts to xmpp2..." entry to an action list. */
void x2_migrate_add_action(GList **actions);

/**
 * Copies one account and its buddy list entries.
 *
 * @param orig  The prpl-jabber account; not modified.
 * @param opts  x2_migrate_map_options() of the two prpls.
 * @return The new, still disabled, account, already added to the account
 *         list and with its archive state seeded; never NULL.
 */
PurpleAccount *x2_migrate_clone_account(PurpleAccount *orig, GList *opts);

/** Copies buddies (groups, contacts, aliases) and room bookmarks. */
void x2_migrate_copy_blist(PurpleAccount *orig, PurpleAccount *clone,
                           guint *buddies, guint *chats);

#endif /* XMPP2_EXT_MIGRATE_H_ */
