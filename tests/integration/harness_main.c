/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 1998-2024 the Pidgin, Finch, and libpurple contributors
 * Copyright (C) 2026 Vincent van Adrighem
 *
 * xmpp2-harness: headless libpurple 2.x client for integration tests.
 * Derived from libpurple/example/nullclient.c (GPLv2+, Pidgin developers).
 *
 * Usage:
 *   xmpp2-harness --prpl prpl-xmpp2 --plugin-dir build \
 *       --user alice@localhost/harness --password secret \
 *       --host 127.0.0.1 --port 15222 [--pref s:name=val|i:name=1|b:name=1]
 *       [--script FILE] [--userdir DIR] [--log txt] [--trust-cert FILE] [--debug]
 *       [--libpref s:|i:|b:/pref/path=VAL]
 *
 *   --userdir DIR     persistent libpurple user dir (state, logs, accounts and
 *                     blist survive restarts). Default: a temp dir removed on exit.
 *                     (--user-dir is accepted as an alias.)
 *   --log txt         enable IM and chat logging in text format.
 *   --trust-cert      pre-seed the tls_peers pool with a PEM for the account
 *                     domain and --host, so a self-signed cert is accepted.
 *   --debug           copy libpurple debug to stderr (always captured internally).
 *
 *
 * Source layout: harness_main.c (this file: args, core init, loop),
 * harness_core.c (run state), harness_ui.c (UI ops, EV lines),
 * harness_filter.c (filters), harness_script.c (command table, --help),
 * harness_cmds_*.c (command handlers). Command reference: --help, or the
 * header comment of harness_script.c.
 */
#include <glib/gstdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "harness.h"

#define PURPLE_GLIB_READ_COND  (G_IO_IN | G_IO_HUP | G_IO_ERR)
#define PURPLE_GLIB_WRITE_COND (G_IO_OUT | G_IO_HUP | G_IO_ERR | G_IO_NVAL)

/* ---- glib event loop glue (verbatim pattern from nullclient.c) ---- */
typedef struct {
	PurpleInputFunction function;
	guint result;
	gpointer data;
} IOClosure;

static gboolean io_invoke(GIOChannel *source, GIOCondition condition, gpointer data)
{
	IOClosure *c = data;
	PurpleInputCondition pc = 0;

	if (condition & PURPLE_GLIB_READ_COND)
		pc |= PURPLE_INPUT_READ;
	if (condition & PURPLE_GLIB_WRITE_COND)
		pc |= PURPLE_INPUT_WRITE;
	c->function(c->data, g_io_channel_unix_get_fd(source), pc);
	return TRUE;
}

static guint glib_input_add(gint fd, PurpleInputCondition condition,
                            PurpleInputFunction function, gpointer data)
{
	IOClosure *c = g_new0(IOClosure, 1);
	GIOChannel *channel = g_io_channel_unix_new(fd);
	GIOCondition cond = 0;

	c->function = function;
	c->data = data;
	if (condition & PURPLE_INPUT_READ)
		cond |= PURPLE_GLIB_READ_COND;
	if (condition & PURPLE_INPUT_WRITE)
		cond |= PURPLE_GLIB_WRITE_COND;
	c->result = g_io_add_watch_full(channel, G_PRIORITY_DEFAULT, cond,
	                                io_invoke, c, g_free);
	g_io_channel_unref(channel);
	return c->result;
}

static PurpleEventLoopUiOps eventloop_ops = {
	g_timeout_add, g_source_remove, glib_input_add, g_source_remove,
	NULL, g_timeout_add_seconds, NULL, NULL, NULL
};

static void set_libpref(const char *spec)
{
	const char *eq = strchr(spec, '=');
	char *path;

	if (!eq || spec[1] != ':') {
		fprintf(stderr, "bad libpref %s (want s:|i:|b:/path=value)\n", spec);
		exit(2);
	}
	path = g_strndup(spec + 2, eq - spec - 2);
	if (!purple_prefs_exists(path)) {
		fprintf(stderr, "libpref %s does not exist (plugin not loaded?)\n", path);
		exit(2);
	}
	if (spec[0] == 's')
		purple_prefs_set_string(path, eq + 1);
	else if (spec[0] == 'i')
		purple_prefs_set_int(path, atoi(eq + 1));
	else
		purple_prefs_set_bool(path, atoi(eq + 1) != 0);
	g_free(path);
}

/* ---- setup ---- */
static void load_script(const char *path)
{
	FILE *fp = path ? fopen(path, "r") : stdin;
	char buf[8192];

	if (!fp) {
		perror(path);
		exit(2);
	}
	while (fgets(buf, sizeof buf, fp))
		g_queue_push_tail(script, g_strdup(buf));
	if (path)
		fclose(fp);
}

/* Remove a directory tree we created with g_dir_make_tmp(). */
static void remove_tree(const char *path)
{
	GDir *dir = g_dir_open(path, 0, NULL);
	const char *name;

	if (dir) {
		while ((name = g_dir_read_name(dir))) {
			char *child = g_build_filename(path, name, NULL);

			if (!g_file_test(child, G_FILE_TEST_IS_SYMLINK) &&
			    g_file_test(child, G_FILE_TEST_IS_DIR))
				remove_tree(child);
			else
				g_remove(child);
			g_free(child);
		}
		g_dir_close(dir);
	}
	g_rmdir(path);
}

/* Copy PEM into <userdir>/certificates/x509/tls_peers/<domain>. */
static void trust_cert(const char *user_dir, const char *domain, const char *pem)
{
	char *dir = g_build_filename(user_dir, "certificates", "x509", "tls_peers", NULL);
	char *dst = g_build_filename(dir, domain, NULL);
	char *data = NULL;
	gsize len = 0;

	if (g_mkdir_with_parents(dir, 0700) != 0 ||
	    !g_file_get_contents(pem, &data, &len, NULL) ||
	    !g_file_set_contents(dst, data, len, NULL)) {
		fprintf(stderr, "cannot install trusted cert %s\n", pem);
		exit(2);
	}
	g_free(data);
	g_free(dst);
	g_free(dir);
}

static void print_usage(FILE *fp)
{
	fprintf(fp, "Usage: xmpp2-harness --user JID [--password PW] [--prpl ID] [--plugin-dir DIR]\n"
	        "    [--host H] [--port N] [--pref s:|i:|b:name=V] [--libpref s:|i:|b:/path=V]\n"
	        "    [--script FILE] [--userdir DIR] [--log txt] [--trust-cert FILE] [--debug] [--help]\n"
	        "The password may also come from XMPP_PASSWORD. Without --script, commands are\n"
	        "read from stdin. Exit 0 on success, 1 on the first FAIL, 2 on setup errors.\n\n");
	harness_print_help(fp);
}

int main(int argc, char **argv)
{
	const char *prpl = "prpl-jabber", *user = NULL, *pass = getenv("XMPP_PASSWORD");
	const char *trust = NULL, *log_fmt = NULL;
	const char *host = NULL, *script_path = NULL, *plugin_dir = NULL, *user_dir = NULL;
	int port = 0, i;
	char *tmpdir = NULL;
	GPtrArray *prefs = g_ptr_array_new(), *libprefs = g_ptr_array_new();

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];
		const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;

		if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
			print_usage(stdout);
			return 0;
		} else if (!strcmp(a, "--debug")) {
			debug_echo = TRUE;
		} else if (!v) {
			fprintf(stderr, "missing value for %s\n", a);
			return 2;
		} else if (!strcmp(a, "--prpl")) { prpl = v; i++; }
		else if (!strcmp(a, "--user")) { user = v; i++; }
		else if (!strcmp(a, "--password")) { pass = v; i++; }
		else if (!strcmp(a, "--host")) { host = v; i++; }
		else if (!strcmp(a, "--port")) { port = atoi(v); i++; }
		else if (!strcmp(a, "--script")) { script_path = v; i++; }
		else if (!strcmp(a, "--plugin-dir")) { plugin_dir = v; i++; }
		else if (!strcmp(a, "--userdir") || !strcmp(a, "--user-dir")) { user_dir = v; i++; }
		else if (!strcmp(a, "--log")) { log_fmt = v; i++; }
		else if (!strcmp(a, "--trust-cert")) { trust = v; i++; }
		else if (!strcmp(a, "--pref")) { g_ptr_array_add(prefs, (gpointer)v); i++; }
		else if (!strcmp(a, "--libpref")) { g_ptr_array_add(libprefs, (gpointer)v); i++; }
		else { fprintf(stderr, "unknown option %s\n", a); return 2; }
	}
	if (!user || !pass) {
		fprintf(stderr, "--user and --password (or XMPP_PASSWORD) required\n");
		return 2;
	}

	signal(SIGCHLD, SIG_IGN);
	/* writes to a dropped socket must fail with EPIPE, not kill us */
	signal(SIGPIPE, SIG_IGN);
	out = fdopen(dup(STDOUT_FILENO), "w");
	dup2(STDERR_FILENO, STDOUT_FILENO);
	if (!user_dir) {
		tmpdir = g_dir_make_tmp("xmpp2-harness-XXXXXX", NULL);
		user_dir = tmpdir;
	} else {
		g_mkdir_with_parents(user_dir, 0700);
	}
	userdir_path = user_dir;
	purple_util_set_user_dir(user_dir);
	if (trust) {
		const char *at = strchr(user, '@');
		char *domain = g_strdup(at ? at + 1 : user);
		char *slash = strchr(domain, '/');

		if (slash)
			*slash = '\0';
		trust_cert(user_dir, domain, trust);
		if (host)
			trust_cert(user_dir, host, trust);
		g_free(domain);
	}
	harness_ui_init();
	purple_eventloop_set_ui_ops(&eventloop_ops);
	if (plugin_dir)
		purple_plugins_add_search_path(plugin_dir);
	if (!purple_core_init(UI_ID)) {
		fprintf(stderr, "libpurple initialization failed\n");
		return 2;
	}
	purple_set_blist(purple_blist_new());
	harness_ui_blist();
	purple_blist_load();
	purple_prefs_load();
	for (i = 0; i < (int)libprefs->len; i++)
		set_libpref(g_ptr_array_index(libprefs, i));
	if (log_fmt) {
		purple_prefs_set_string("/purple/logging/format", log_fmt);
		purple_prefs_set_bool("/purple/logging/log_ims", TRUE);
		purple_prefs_set_bool("/purple/logging/log_chats", TRUE);
	}

	if (!purple_find_prpl(prpl)) {
		GList *l;

		fprintf(stderr, "protocol %s not found; available:", prpl);
		for (l = purple_plugins_get_protocols(); l; l = l->next)
			fprintf(stderr, " %s", ((PurplePlugin *)l->data)->info->id);
		fprintf(stderr, "\n");
		return 2;
	}

	account = purple_accounts_find(user, prpl);
	if (!account) {
		account = purple_account_new(user, prpl);
		purple_accounts_add(account);
	}
	purple_account_set_password(account, pass);
	purple_account_set_remember_password(account, FALSE);
	/* Self-signed test cert is trusted via --trust-cert; plain auth is allowed. */
	purple_account_set_string(account, "connection_security", "opportunistic_tls");
	purple_account_set_bool(account, "auth_plain_in_clear", TRUE);
	if (host)
		purple_account_set_string(account, "connect_server", host);
	if (port)
		purple_account_set_int(account, "port", port);
	c2s_port = port ? port : 5222;
	for (i = 0; i < (int)prefs->len; i++)
		set_pref(account, g_ptr_array_index(prefs, i));

	harness_connect_signals();

	script = g_queue_new();
	load_script(script_path);

	loop = g_main_loop_new(NULL, FALSE);
	g_idle_add(next_command, NULL);
	g_main_loop_run(loop);

	if (account != NULL)
		purple_account_set_enabled(account, UI_ID, FALSE);
	purple_core_quit();
	if (tmpdir)
		remove_tree(tmpdir);
	return exit_code;
}
