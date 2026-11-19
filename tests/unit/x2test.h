/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/*
 * Shared setup of the unit tests.
 *
 * xmlnode_new() registers every node with libpurple's D-Bus pointer map,
 * which normally only a full purple core initialises; without it each node
 * logs a GLib critical.  purple_dbus_init_ids() sets up just that map, no
 * D-Bus connection.  It only exists in libpurple builds with D-Bus, where it
 * is needed, so it is referenced weakly.  purple_util_init() is the only
 * other piece of core state the code under test relies on
 * (purple_str_to_time).
 *
 * Warnings and criticals stay fatal (g_test_init()), so a failed
 * g_return_if_fail() fails the test unless it is announced with
 * g_test_expect_message().
 */
#ifndef X2TEST_H_
#define X2TEST_H_

#include <glib.h>

#include "util.h"

extern void purple_dbus_init_ids(void) __attribute__((weak));

static void
x2_test_init(int *argc, char ***argv)
{
	g_test_init(argc, argv, NULL);
	if (purple_dbus_init_ids != NULL)
		purple_dbus_init_ids();
	/* compiles the regexes behind purple_str_to_time() */
	purple_util_init();
}

#endif /* X2TEST_H_ */
