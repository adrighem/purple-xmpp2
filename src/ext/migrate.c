/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file migrate.c Copy of prpl-jabber accounts to prpl-xmpp2 (ARCHITECTURE 10)
 *
 * Three entry points ask for confirmation and then run the same copy: the
 * prpl action (every UI, but Pidgin shows prpl actions only for a connected
 * account of that prpl), a plugin action under Tools > Plugin Actions that
 * needs no xmpp2 account, and a one-time offer at startup (Pidgin only).
 * Libpurple loads protocol plugins before it loads the accounts, so the
 * offer is deferred with a timer.
 */
#include "xmpp2_internal.h"

#include "core.h"
#include "debug.h"
#include "eventloop.h"
#include "notify.h"
#include "prefs.h"
#include "request.h"
#include "version.h"

#include "core/migrate_map.h"
#include "ext/migrate.h"

#define DBG X2_MIGRATE_DEBUG

/* Pidgin's historical UI id (pidgin/pidgin.h); Finch and others get no offer */
#define PIDGIN_UI_ID "gtk-gaim"

/* Seconds after plugin load: the accounts and the main window are there */
#define OFFER_DELAY_SECS 2

static PurplePlugin *plugin_handle;
static PurplePlugin *actions_plugin;
static guint offer_timer;
static gboolean copy_running;

/* ---- the originals ---- */

static gboolean
has_twin(PurpleAccount *orig)
{
	GList *l;

	for (l = purple_accounts_get_all(); l != NULL; l = l->next) {
		PurpleAccount *a = l->data;

		/* the same bare JID is the same account: the resource is only
		 * how it logs in, and a second session with it would conflict */
		if (purple_strequal(purple_account_get_protocol_id(a),
		                    XMPP2_PRPL_ID) &&
		    x2_migrate_same_username(purple_account_get_username(a),
		                             purple_account_get_username(orig)))
			return TRUE;
	}
	return FALSE;
}

/* The accounts to copy: newly allocated list, accounts not owned. */
static GList *
pending_accounts(void)
{
	GList *out = NULL, *l;

	for (l = purple_accounts_get_all(); l != NULL; l = l->next)
		if (purple_strequal(purple_account_get_protocol_id(l->data),
		                    X2_MIGRATE_ORIGINAL_PRPL) &&
		    !has_twin(l->data))
			out = g_list_append(out, l->data);
	return out;
}

guint
x2_migrate_pending(void)
{
	GList *l = pending_accounts();
	guint n = g_list_length(l);

	g_list_free(l);
	return n;
}

/* ---- the copy ---- */

static GList *
option_map(void)
{
	PurplePlugin *from = purple_find_prpl(X2_MIGRATE_ORIGINAL_PRPL);
	PurplePlugin *to = purple_find_prpl(XMPP2_PRPL_ID);

	if (from == NULL || to == NULL)
		return NULL;
	return x2_migrate_map_options(
		PURPLE_PLUGIN_PROTOCOL_INFO(from)->protocol_options,
		PURPLE_PLUGIN_PROTOCOL_INFO(to)->protocol_options);
}

/* Copies one account; the original goes off before the clone comes on:
 * both would use the same full JID, and the server would kick one of them
 * with a resource conflict, again and again. */
static void
migrate_one(PurpleAccount *orig, GList *opts)
{
	const char *ui = purple_core_get_ui();
	gboolean enabled = purple_account_get_enabled(orig, ui);
	PurpleAccount *clone;
	guint buddies, chats;

	clone = x2_migrate_clone_account(orig, opts);
	x2_migrate_copy_blist(orig, clone, &buddies, &chats);
	purple_account_set_enabled(orig, ui, FALSE);
	if (enabled)
		purple_account_set_enabled(clone, ui, TRUE);
	purple_debug_info(DBG, "%s: original disabled, copy %s\n",
	                  purple_account_get_username(orig),
	                  enabled ? "enabled" : "left disabled");
}

static guint
run_copy(void)
{
	GList *todo = pending_accounts(), *opts, *l;
	guint n = g_list_length(todo);

	/* a login in between may re-enter through a UI main loop */
	if (copy_running) {
		g_list_free(todo);
		return 0;
	}
	copy_running = TRUE;
	opts = option_map();
	for (l = todo; l != NULL; l = l->next)
		migrate_one(l->data, opts);
	x2_migrate_opts_free(opts);
	g_list_free(todo);
	copy_running = FALSE;
	return n;
}

/* ---- confirmation and result ---- */

static void
report(guint n)
{
	char *msg;

	if (n == 0) {
		purple_notify_info(plugin_handle, _("Copy XMPP accounts"),
		                   _("Nothing to copy"),
		                   _("Every XMPP account already has an xmpp2 "
		                     "copy, or there is none."));
		return;
	}
	msg = g_strdup_printf(n == 1 ? _("Copied %u XMPP account to xmpp2")
	                             : _("Copied %u XMPP accounts to xmpp2"), n);
	purple_notify_info(plugin_handle, _("Copy XMPP accounts"), msg,
	                   _("The originals are kept but disabled."));
	g_free(msg);
}

static void
copy_cb(gpointer unused, int choice)
{
	report(run_copy());
}

static void
never_cb(gpointer unused, int choice)
{
	purple_prefs_set_bool(X2_MIGRATE_PREF_NEVER, TRUE);
	purple_debug_info(DBG, "offer disabled for good\n");
}

static void
ask(gboolean with_never)
{
	guint n = x2_migrate_pending();
	char *primary;

	if (n == 0) {
		report(0);
		return;
	}
	purple_debug_info(DBG, "asking to copy %u account(s)%s\n", n,
	                  with_never ? " (startup offer)" : "");
	primary = g_strdup_printf(n == 1
		? _("Copy %u XMPP account to XMPP (xmpp2)?")
		: _("Copy %u XMPP accounts to XMPP (xmpp2)?"), n);
	if (with_never)
		purple_request_action(plugin_handle, _("XMPP (xmpp2)"), primary,
			_("The originals are kept but disabled. The copies "
			  "start without replaying old history."),
			0, NULL, NULL, NULL, NULL, 3,
			_("Copy"), G_CALLBACK(copy_cb),
			_("Not now"), NULL,
			_("Never"), G_CALLBACK(never_cb));
	else
		purple_request_action(plugin_handle, _("XMPP (xmpp2)"), primary,
			_("The originals are kept but disabled. The copies "
			  "start without replaying old history."),
			0, NULL, NULL, NULL, NULL, 2,
			_("Copy"), G_CALLBACK(copy_cb),
			_("Cancel"), NULL);
	g_free(primary);
}

/* ---- entry points ---- */

static void
action_cb(PurplePluginAction *action)
{
	ask(FALSE);
}

void
x2_migrate_add_action(GList **actions)
{
	*actions = g_list_append(*actions, purple_plugin_action_new(
		_("Copy XMPP accounts to xmpp2..."), action_cb));
}

/*
 * Pidgin lists the actions of a protocol plugin only under the menu of a
 * connected account of it (and never under Tools, which skips protocol
 * plugins), so with no xmpp2 account the copy would be out of reach.  A
 * second, invisible plugin of the standard type lives as long as this one
 * and gives Tools > Plugin Actions > XMPP (xmpp2) the same entry.
 * Invisible: it is not in the plugin list, so a user cannot unload it.
 */
static GList *
actions_plugin_actions(PurplePlugin *plugin, gpointer context)
{
	GList *actions = NULL;

	x2_migrate_add_action(&actions);
	return actions;
}

static PurplePluginInfo actions_info = {
	PURPLE_PLUGIN_MAGIC,
	PURPLE_MAJOR_VERSION,
	PURPLE_MINOR_VERSION,
	PURPLE_PLUGIN_STANDARD,                           /**< type           */
	NULL,                                             /**< ui_requirement */
	PURPLE_PLUGIN_FLAG_INVISIBLE,                     /**< flags          */
	NULL,                                             /**< dependencies   */
	PURPLE_PRIORITY_DEFAULT,                          /**< priority       */

	X2_MIGRATE_ACTIONS_PLUGIN_ID,                     /**< id             */
	"XMPP (xmpp2)",                                   /**< name           */
	XMPP2_VERSION,                                    /**< version        */
	N_("Copy XMPP accounts to xmpp2"),                /**< summary        */
	N_("Copy XMPP accounts to xmpp2"),                /**< description    */
	NULL,                                             /**< author         */
	XMPP2_WEBSITE,                                    /**< homepage       */

	NULL,                                             /**< load           */
	NULL,                                             /**< unload         */
	NULL,                                             /**< destroy        */

	NULL,                                             /**< ui_info        */
	NULL,                                             /**< extra_info     */
	NULL,                                             /**< prefs_info     */
	actions_plugin_actions,                           /**< actions        */

	NULL, NULL, NULL, NULL                            /* padding */
};

static void
register_actions_plugin(void)
{
	actions_plugin = purple_plugin_new(TRUE, NULL);
	actions_plugin->info = &actions_info;
	if (!purple_plugin_register(actions_plugin) ||
	    !purple_plugin_load(actions_plugin)) {
		purple_debug_error(DBG, "cannot register the Tools menu entry\n");
		purple_plugin_destroy(actions_plugin);
		actions_plugin = NULL;
	}
}

static void
unregister_actions_plugin(void)
{
	/* its info lives in this module: it must be gone before we are */
	if (actions_plugin != NULL)
		purple_plugin_destroy(actions_plugin);
	actions_plugin = NULL;
}

static gboolean
offer_cb(gpointer unused)
{
	offer_timer = 0;
	if (purple_prefs_get_bool(X2_MIGRATE_PREF_NEVER))
		return FALSE;
	if (!purple_strequal(purple_core_get_ui(), PIDGIN_UI_ID)
#ifdef XMPP2_TEST_HOOKS
	    && !purple_prefs_get_bool(X2_MIGRATE_PREF_ANY_UI)
#endif
	    )
		return FALSE;
	if (x2_migrate_pending() == 0)
		return FALSE;
	ask(TRUE);
	return FALSE;
}

static void
migrate_plugin_init(PurplePlugin *plugin)
{
	plugin_handle = plugin;
	purple_prefs_add_bool(X2_MIGRATE_PREF_NEVER, FALSE);
#ifdef XMPP2_TEST_HOOKS
	purple_prefs_add_bool(X2_MIGRATE_PREF_ANY_UI, FALSE);
#endif
	register_actions_plugin();
	offer_timer = purple_timeout_add_seconds(OFFER_DELAY_SECS, offer_cb,
	                                         NULL);
}

static void
migrate_plugin_uninit(PurplePlugin *plugin)
{
	if (offer_timer != 0)
		purple_timeout_remove(offer_timer);
	offer_timer = 0;
	purple_request_close_with_handle(plugin);
	unregister_actions_plugin();
	plugin_handle = NULL;
}

const X2Ext x2_ext_migrate = {
	.name = "migrate",
	.plugin_init = migrate_plugin_init,
	.plugin_uninit = migrate_plugin_uninit,
};
