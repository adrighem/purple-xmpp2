/*
 * Shared setup of the unit tests.
 *
 * xmlnode_new() registers the node with libpurple's D-Bus pointer map, which
 * is only initialised by a full purple core.  Without it libpurple logs a
 * critical that g_test_init() would make fatal, so drop exactly those.
 * purple_util_init() is the only other piece of core state the code under
 * test relies on (purple_str_to_time).
 */
#ifndef X2TEST_H_
#define X2TEST_H_

#include <string.h>
#include <glib.h>

#include "util.h"

static void
x2_test_log_handler(const gchar *domain, GLogLevelFlags level,
                    const gchar *message, gpointer data)
{
	if (message != NULL && strstr(message, "purple_dbus_") != NULL)
		return;
	g_log_default_handler(domain, level, message, data);
}

static void
x2_test_init(int *argc, char ***argv)
{
	g_test_init(argc, argv, NULL);
	/* g_test_init() made criticals fatal; keep that for everything else
	 * by only silencing the D-Bus ones and re-arming through the handler */
	g_log_set_always_fatal(G_LOG_FATAL_MASK);
	g_log_set_default_handler(x2_test_log_handler, NULL);
	/* compiles the regexes behind purple_str_to_time() */
	purple_util_init();
}

#endif /* X2TEST_H_ */
