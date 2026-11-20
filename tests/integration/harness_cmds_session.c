/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */

/* Session commands: connect, drop, pref, buddy-add, accounts, prpl actions. */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "harness.h"

void cmd_connect(const char *cmd, char *arg)
{
	double secs = *arg ? atof(arg) : 20;

	arm(P_CONNECT, "connect", secs);
	purple_account_set_enabled(account, UI_ID, TRUE);
	purple_savedstatus_activate(purple_savedstatus_new(NULL, PURPLE_STATUS_AVAILABLE));
	if (purple_account_is_connected(account))
		resume(TRUE, NULL);
}

void cmd_disconnect(const char *cmd, char *arg)
{
	arm(P_DISCONNECT, "disconnect", 10);
	if (!purple_account_is_connected(account)) {
		resume(TRUE, "already offline");
		return;
	}
	purple_account_set_enabled(account, UI_ID, FALSE);
	purple_account_disconnect(account);
}

/* Shuts down our TCP sockets whose peer port is the c2s port. */
int drop_c2s_sockets(void)
{
	GDir *d = g_dir_open("/proc/self/fd", 0, NULL);
	const char *name;
	int n = 0;

	while (d && (name = g_dir_read_name(d))) {
		int fd = atoi(name);
		struct sockaddr_storage sa;
		socklen_t len = sizeof(sa);
		struct stat st;
		int port = -1;

		if (fstat(fd, &st) != 0 || !S_ISSOCK(st.st_mode) ||
		    getpeername(fd, (struct sockaddr *)&sa, &len) != 0)
			continue;
		if (sa.ss_family == AF_INET)
			port = ntohs(((struct sockaddr_in *)&sa)->sin_port);
		else if (sa.ss_family == AF_INET6)
			port = ntohs(((struct sockaddr_in6 *)&sa)->sin6_port);
		if (port == c2s_port && shutdown(fd, SHUT_RDWR) == 0)
			n++;
	}
	if (d)
		g_dir_close(d);
	return n;
}

void cmd_drop(const char *cmd, char *arg)
{
	int n;
	char *m;

	if (!need_online("drop"))
		return;
	n = drop_c2s_sockets();
	if (n == 0) {
		fail_now("drop", "no socket connected to the c2s port");
		return;
	}
	m = g_strdup_printf("%d socket(s)", n);
	continue_ok("drop", m);
	g_free(m);
}

void cmd_sleep(const char *cmd, char *arg)
{
	arm(P_SLEEP, "sleep", atof(arg));
}

void cmd_quit(const char *cmd, char *arg)
{
	finish(0);
}

void cmd_remove_account(const char *cmd, char *arg)
{
	purple_accounts_delete(account);
	account = NULL;
	say(TRUE, "remove-account", NULL);
	finish(0);
}

void set_pref(PurpleAccount *acct, const char *spec)
{
	const char *eq = strchr(spec, '=');
	char *name;

	if (!eq || spec[1] != ':') {
		fprintf(stderr, "bad pref %s (want s:|i:|b:name=value)\n", spec);
		exit(2);
	}
	name = g_strndup(spec + 2, eq - spec - 2);
	if (spec[0] == 's')
		purple_account_set_string(acct, name, eq + 1);
	else if (spec[0] == 'i')
		purple_account_set_int(acct, name, atoi(eq + 1));
	else
		purple_account_set_bool(acct, name, atoi(eq + 1) != 0);
	g_free(name);
}

void cmd_pref(const char *cmd, char *arg)
{
	set_pref(account, arg);
	continue_ok("pref", arg);
}

void cmd_buddy_add(const char *cmd, char *arg)
{
	PurpleBuddy *b;

	if (!need_online("buddy-add"))
		return;
	b = purple_find_buddy(account, arg);
	if (!b) {
		b = purple_buddy_new(account, arg, NULL);
		purple_blist_add_buddy(b, NULL, NULL, NULL);
	}
	purple_account_add_buddy(account, b);
	continue_ok("buddy-add", arg);
}

static char *acct_prpl, *acct_user;
static int want_exists, want_enabled, want_connected;

static gboolean poll_account(char **detail)
{
	PurpleAccount *a = purple_accounts_find(acct_user, acct_prpl);

	if (!want_exists)
		return a == NULL;
	if (!a)
		return FALSE;
	if (want_enabled >= 0 && purple_account_get_enabled(a, UI_ID) != want_enabled)
		return FALSE;
	if (want_connected >= 0 && purple_account_is_connected(a) != want_connected)
		return FALSE;
	return TRUE;
}

void cmd_expect_account(const char *cmd, char *arg)
{
	char **tok = g_strsplit(arg, " ", -1);
	double secs = 10;
	int i;

	want_exists = 1;
	want_enabled = want_connected = -1;
	if (g_strv_length(tok) < 2) {
		fail_now(cmd, "usage 'expect-account PRPL USER [exists=] [enabled=] [connected=] [timeout=]'");
		g_strfreev(tok);
		return;
	}
	g_free(acct_prpl);
	g_free(acct_user);
	acct_prpl = g_strdup(tok[0]);
	acct_user = g_strdup(tok[1]);
	for (i = 2; tok[i]; i++) {
		if (g_str_has_prefix(tok[i], "exists="))
			want_exists = atoi(tok[i] + 7);
		else if (g_str_has_prefix(tok[i], "enabled="))
			want_enabled = atoi(tok[i] + 8);
		else if (g_str_has_prefix(tok[i], "connected="))
			want_connected = atoi(tok[i] + 10);
		else if (g_str_has_prefix(tok[i], "timeout="))
			secs = atof(tok[i] + 8);
		else {
			fail_now(cmd, "unknown key");
			g_strfreev(tok);
			return;
		}
	}
	g_strfreev(tok);
	poll_arm(P_POLL, cmd, secs, poll_account);
}

/* Runs the prpl action whose label contains text, as the Accounts menu does. */
void cmd_action(const char *cmd, char *text)
{
	PurpleConnection *gc = purple_account_get_connection(account);
	PurplePlugin *prpl = gc ? purple_connection_get_prpl(gc) : NULL;
	GList *acts, *l;
	gboolean hit = FALSE;

	if (!prpl || !PURPLE_PLUGIN_HAS_ACTIONS(prpl)) {
		fail_now(cmd, "account is not connected or has no actions");
		return;
	}
	acts = PURPLE_PLUGIN_ACTIONS(prpl, gc);
	for (l = acts; l; l = l->next) {
		PurplePluginAction *act = l->data;

		if (!hit && act && act->label && strstr(act->label, text)) {
			act->plugin = prpl;
			act->context = gc;
			hit = TRUE;
			act->callback(act);
		}
	}
	for (l = acts; l; l = l->next)
		if (l->data)
			purple_plugin_action_free(l->data);
	g_list_free(acts);
	if (hit)
		continue_ok(cmd, text);
	else
		fail_now(cmd, "no action with that label");
}

/* Runs the action of a standard plugin, as Tools > Plugin Actions does:
 * "ID TEXT", no context. Protocol plugins are not in that menu. */
void cmd_plugin_action(const char *cmd, char *arg)
{
	char *id = g_strdup(arg), *text = strchr(id, ' ');
	PurplePlugin *plugin;
	GList *acts, *l;
	gboolean hit = FALSE;

	if (text == NULL) {
		g_free(id);
		fail_now(cmd, "want ID TEXT");
		return;
	}
	*text++ = '\0';
	plugin = purple_plugins_find_with_id(id);
	if (plugin == NULL || !purple_plugin_is_loaded(plugin) ||
	    PURPLE_IS_PROTOCOL_PLUGIN(plugin) ||
	    !PURPLE_PLUGIN_HAS_ACTIONS(plugin)) {
		g_free(id);
		fail_now(cmd, "no such loaded plugin with actions");
		return;
	}
	acts = PURPLE_PLUGIN_ACTIONS(plugin, NULL);
	for (l = acts; l; l = l->next) {
		PurplePluginAction *act = l->data;

		if (!hit && act && act->label && strstr(act->label, text)) {
			act->plugin = plugin;
			act->context = NULL;
			hit = TRUE;
			act->callback(act);
		}
	}
	for (l = acts; l; l = l->next)
		if (l->data)
			purple_plugin_action_free(l->data);
	g_list_free(acts);
	g_free(id);
	if (hit)
		continue_ok(cmd, text);
	else
		fail_now(cmd, "no action with that label");
}
