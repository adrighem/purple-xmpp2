/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file statefile.c Persistence glue of the archive state
 */
#include "xmpp2_internal.h"

#include <errno.h>
#include <glib/gstdio.h>

#include "debug.h"
#include "eventloop.h"
#include "util.h"

#include "ext/statefile.h"

#define X2_STATE_DEBOUNCE_SECS 2

struct _X2StateFile {
	char *path;
	X2Store *store;
	guint timer;
};

char *
x2_statefile_path(PurpleAccount *account)
{
	char *name, *path;

	g_return_val_if_fail(account != NULL, NULL);

	/* The full username, resource included: two accounts of one bare JID
	 * never share a file. */
	name = g_strconcat(purple_escape_filename(
	                       purple_account_get_username(account)),
	                   ".state", NULL);
	path = g_build_filename(purple_user_dir(), "xmpp2", name, NULL);
	g_free(name);
	return path;
}

X2StateFile *
x2_statefile_open(PurpleAccount *account)
{
	X2StateFile *sf;

	g_return_val_if_fail(account != NULL, NULL);

	sf = g_new0(X2StateFile, 1);
	sf->path = x2_statefile_path(account);
	sf->store = x2_store_load(sf->path);
	return sf;
}

X2Store *
x2_statefile_store(X2StateFile *sf)
{
	g_return_val_if_fail(sf != NULL, NULL);
	return sf->store;
}

void
x2_statefile_flush(X2StateFile *sf)
{
	GError *err = NULL;
	char *dir;

	g_return_if_fail(sf != NULL);

	if (sf->timer != 0) {
		purple_timeout_remove(sf->timer);
		sf->timer = 0;
	}
	if (!x2_store_is_dirty(sf->store))
		return;

	dir = g_path_get_dirname(sf->path);
	if (purple_build_dir(dir, 0700) != 0)
		purple_debug_error("xmpp2-mam", "cannot create %s: %s\n", dir,
		                   g_strerror(errno));
	else if (!x2_store_save(sf->store, sf->path, &err)) {
		purple_debug_error("xmpp2-mam", "cannot write %s: %s\n",
		                   sf->path, err->message);
		g_error_free(err);
	} else
		purple_debug_misc("xmpp2-mam", "state saved to %s\n", sf->path);
	g_free(dir);
}

static gboolean
debounce_cb(gpointer data)
{
	X2StateFile *sf = data;

	sf->timer = 0;
	x2_statefile_flush(sf);
	return FALSE;
}

void
x2_statefile_touch(X2StateFile *sf)
{
	g_return_if_fail(sf != NULL);

	if (sf->timer == 0 && x2_store_is_dirty(sf->store))
		sf->timer = purple_timeout_add_seconds(X2_STATE_DEBOUNCE_SECS,
		                                       debounce_cb, sf);
}

void
x2_statefile_close(X2StateFile *sf)
{
	if (sf == NULL)
		return;
	x2_statefile_flush(sf);
	x2_store_free(sf->store);
	g_free(sf->path);
	g_free(sf);
}

void
x2_statefile_remove(PurpleAccount *account)
{
	char *path = x2_statefile_path(account);

	if (path == NULL)
		return;
	if (g_unlink(path) == 0)
		purple_debug_info("xmpp2-mam", "removed %s\n", path);
	else if (errno != ENOENT)
		purple_debug_error("xmpp2-mam", "cannot remove %s: %s\n", path,
		                   g_strerror(errno));
	g_free(path);
}
