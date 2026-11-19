/*
 * xmpp2-harness: headless libpurple 2.x client for integration tests.
 * Derived from libpurple/example/nullclient.c (GPLv2+, Pidgin developers).
 *
 * Usage:
 *   xmpp2-harness --prpl prpl-xmpp2 --plugin-dir build \
 *       --user alice@localhost/harness --password secret \
 *       --host 127.0.0.1 --port 15222 [--pref s:name=val|i:name=1|b:name=1]
 *       [--script FILE] [--user-dir DIR] [--trust-cert FILE] [--debug]
 *
 * --trust-cert pre-seeds libpurple's tls_peers pool with a PEM certificate for
 * the account domain, so a self-signed server cert is accepted headlessly.
 *
 * Commands (one per line, from --script or stdin; '#' starts a comment):
 *   connect [SECS]       enable the account, wait for sign-on (default 20)
 *   disconnect           disable the account and wait for sign-off
 *   send JID TEXT        send an IM
 *   wait TEXT [SECS]     wait for a received IM containing TEXT (default 20);
 *                        messages already received count too
 *   absent TEXT [SECS]   assert no received IM contains TEXT for SECS (default 3)
 *   count TEXT N         assert exactly N received IMs contain TEXT so far
 *   sleep SECS           pause
 *   quit                 stop (implicit at end of input)
 * Each command prints "OK ..." or "FAIL ...". Exit 0 when all passed, 1 on
 * the first failure, 2 on setup errors. Received/sent messages are logged as
 * "MSG recv|send <who>: <text>"; with --debug libpurple debug goes to stderr.
 */
#include <glib.h>
#include <glib/gstdio.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "purple.h"

#define UI_ID "xmpp2harness"
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

/* ---- debug ---- */
static gboolean debug_on;

static void debug_print(PurpleDebugLevel level, const char *category, const char *arg)
{
	fprintf(stderr, "DEBUG[%d] %s: %s", (int)level, category ? category : "", arg);
}

static gboolean debug_enabled(PurpleDebugLevel level, const char *category)
{
	(void)level;
	(void)category;
	return debug_on;
}

static PurpleDebugUiOps debug_ops = { debug_print, debug_enabled, NULL, NULL, NULL, NULL };

/* ---- state ---- */
typedef enum { P_NONE, P_CONNECT, P_DISCONNECT, P_WAIT, P_ABSENT, P_SLEEP } Pending;

static GMainLoop *loop;
static PurpleAccount *account;
static GQueue *script;        /* of char* */
static GPtrArray *received;   /* of char* (plain text of received IMs) */
static Pending pending = P_NONE;
static char *pending_text;
static char *pending_cmd;
static guint pending_timer;
static int exit_code;

static gboolean next_command(gpointer unused);

static void finish(int code)
{
	exit_code = code;
	g_main_loop_quit(loop);
}

static void resume(gboolean ok, const char *detail)
{
	if (pending_timer)
		g_source_remove(pending_timer);
	pending_timer = 0;
	pending = P_NONE;
	printf("%s %s%s%s\n", ok ? "OK" : "FAIL", pending_cmd,
	       detail ? ": " : "", detail ? detail : "");
	fflush(stdout);
	g_free(pending_text);
	pending_text = NULL;
	if (!ok) {
		finish(1);
		return;
	}
	g_idle_add(next_command, NULL);
}

static gboolean on_timeout(gpointer data)
{
	(void)data;
	pending_timer = 0;
	switch (pending) {
	case P_ABSENT:
	case P_SLEEP:
		resume(TRUE, NULL);
		break;
	case P_CONNECT:
		resume(FALSE, "timeout waiting for sign-on");
		break;
	case P_DISCONNECT:
		resume(FALSE, "timeout waiting for sign-off");
		break;
	default:
		resume(FALSE, "timeout");
		break;
	}
	return FALSE;
}

static void arm(Pending p, const char *cmd, const char *text, double secs)
{
	pending = p;
	g_free(pending_cmd);
	pending_cmd = g_strdup(cmd);
	g_free(pending_text);
	pending_text = text ? g_strdup(text) : NULL;
	pending_timer = g_timeout_add((guint)(secs * 1000), on_timeout, NULL);
}

static int count_matching(const char *text)
{
	guint i;
	int n = 0;

	for (i = 0; i < received->len; i++)
		if (strstr(g_ptr_array_index(received, i), text))
			n++;
	return n;
}

/* ---- signals ---- */
static void on_wrote_im(PurpleAccount *acct, const char *who, char *message,
                        PurpleConversation *conv, PurpleMessageFlags flags)
{
	char *plain;

	(void)conv;
	if (acct != account || !message)
		return;
	plain = purple_markup_strip_html(message);
	if (flags & PURPLE_MESSAGE_SEND) {
		printf("MSG send %s: %s\n", who, plain);
		g_free(plain);
	} else if (flags & PURPLE_MESSAGE_RECV) {
		printf("MSG recv %s%s: %s\n", who,
		       (flags & PURPLE_MESSAGE_DELAYED) ? " (delayed)" : "", plain);
		g_ptr_array_add(received, plain);
		if (pending == P_WAIT && strstr(plain, pending_text))
			resume(TRUE, pending_text);
		else if (pending == P_ABSENT && strstr(plain, pending_text))
			resume(FALSE, "unexpected message seen");
	} else {
		g_free(plain);
	}
	fflush(stdout);
}

static void on_signed_on(PurpleConnection *gc)
{
	if (purple_connection_get_account(gc) == account && pending == P_CONNECT)
		resume(TRUE, NULL);
}

static void on_signed_off(PurpleConnection *gc)
{
	if (purple_connection_get_account(gc) == account && pending == P_DISCONNECT)
		resume(TRUE, NULL);
}

static void on_conn_error(PurpleConnection *gc, PurpleConnectionError err,
                          const char *desc)
{
	char *msg;

	if (purple_connection_get_account(gc) != account)
		return;
	fprintf(stderr, "connection error %d: %s\n", (int)err, desc ? desc : "");
	if (pending == P_CONNECT) {
		msg = g_strdup_printf("connection error %d: %s", (int)err, desc ? desc : "");
		resume(FALSE, msg);
		g_free(msg);
	}
}

/* ---- commands ---- */
static void do_command(char *line)
{
	char *cmd = g_strstrip(line);
	char *arg = strchr(cmd, ' ');
	double secs;

	if (arg)
		*arg++ = '\0';
	if (!*cmd || *cmd == '#') {
		g_idle_add(next_command, NULL);
		return;
	}
	arg = arg ? g_strstrip(arg) : (char *)"";

	if (!strcmp(cmd, "connect")) {
		secs = *arg ? atof(arg) : 20;
		arm(P_CONNECT, "connect", NULL, secs);
		purple_account_set_enabled(account, UI_ID, TRUE);
		purple_savedstatus_activate(purple_savedstatus_new(NULL, PURPLE_STATUS_AVAILABLE));
		if (purple_account_is_connected(account))
			resume(TRUE, NULL);
	} else if (!strcmp(cmd, "disconnect")) {
		arm(P_DISCONNECT, "disconnect", NULL, 10);
		if (!purple_account_is_connected(account)) {
			resume(TRUE, "already offline");
			return;
		}
		purple_account_set_enabled(account, UI_ID, FALSE);
		purple_account_disconnect(account);
	} else if (!strcmp(cmd, "send")) {
		char *text = strchr(arg, ' ');
		PurpleConversation *conv;

		if (!text || !purple_account_is_connected(account)) {
			printf("FAIL send: usage 'send JID TEXT' and must be connected\n");
			finish(1);
			return;
		}
		*text++ = '\0';
		conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, arg);
		purple_conv_im_send(purple_conversation_get_im_data(conv), text);
		printf("OK send %s\n", arg);
		fflush(stdout);
		g_idle_add(next_command, NULL);
	} else if (!strcmp(cmd, "wait") || !strcmp(cmd, "absent")) {
		gboolean w = !strcmp(cmd, "wait");
		char *sp = strrchr(arg, ' ');
		char *text = g_strdup(arg);

		secs = w ? 20 : 3;
		if (sp && atof(sp + 1) > 0 && g_ascii_isdigit(sp[1])) {
			secs = atof(sp + 1);
			text[sp - arg] = '\0';
		}
		arm(w ? P_WAIT : P_ABSENT, w ? "wait" : "absent", text, secs);
		if (w && count_matching(text))
			resume(TRUE, text);
		else if (!w && count_matching(text))
			resume(FALSE, "message already seen");
		g_free(text);
	} else if (!strcmp(cmd, "count")) {
		char *sp = strrchr(arg, ' ');
		int want, got;

		if (!sp) {
			printf("FAIL count: usage 'count TEXT N'\n");
			finish(1);
			return;
		}
		want = atoi(sp + 1);
		*sp = '\0';
		got = count_matching(arg);
		printf("%s count '%s': want %d got %d\n", got == want ? "OK" : "FAIL", arg, want, got);
		fflush(stdout);
		if (got != want) {
			finish(1);
			return;
		}
		g_idle_add(next_command, NULL);
	} else if (!strcmp(cmd, "sleep")) {
		arm(P_SLEEP, "sleep", NULL, atof(arg));
	} else if (!strcmp(cmd, "quit")) {
		finish(0);
	} else {
		printf("FAIL unknown command: %s\n", cmd);
		finish(1);
	}
}

static gboolean next_command(gpointer unused)
{
	char *line = g_queue_pop_head(script);

	(void)unused;
	if (!line) {
		finish(0);
		return FALSE;
	}
	do_command(line);
	g_free(line);
	return FALSE;
}

/* ---- setup ---- */
static void load_script(const char *path)
{
	FILE *fp = path ? fopen(path, "r") : stdin;
	char buf[4096];

	if (!fp) {
		perror(path);
		exit(2);
	}
	while (fgets(buf, sizeof buf, fp))
		g_queue_push_tail(script, g_strdup(buf));
	if (path)
		fclose(fp);
}

static void set_pref(PurpleAccount *acct, const char *spec)
{
	const char *eq = strchr(spec, '=');
	char *name;

	if (!eq || spec[1] != ':') {
		fprintf(stderr, "bad --pref %s (want s:|i:|b:name=value)\n", spec);
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

/* Remove a directory tree we created with g_dir_make_tmp(). */
static void remove_tree(const char *path)
{
	GDir *dir = g_dir_open(path, 0, NULL);
	const char *name;

	if (dir) {
		while ((name = g_dir_read_name(dir))) {
			char *child = g_build_filename(path, name, NULL);

			if (g_file_test(child, G_FILE_TEST_IS_DIR))
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

int main(int argc, char **argv)
{
	const char *prpl = "prpl-jabber", *user = NULL, *pass = getenv("XMPP_PASSWORD");
	const char *trust = NULL;
	const char *host = NULL, *script_path = NULL, *plugin_dir = NULL, *user_dir = NULL;
	int port = 0, i;
	char *tmpdir = NULL;
	GPtrArray *prefs = g_ptr_array_new();
	static int sig_handle;

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];
		const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;

		if (!strcmp(a, "--debug")) {
			debug_on = TRUE;
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
		else if (!strcmp(a, "--user-dir")) { user_dir = v; i++; }
		else if (!strcmp(a, "--trust-cert")) { trust = v; i++; }
		else if (!strcmp(a, "--pref")) { g_ptr_array_add(prefs, (gpointer)v); i++; }
		else { fprintf(stderr, "unknown option %s\n", a); return 2; }
	}
	if (!user || !pass) {
		fprintf(stderr, "--user and --password (or XMPP_PASSWORD) required\n");
		return 2;
	}

	signal(SIGCHLD, SIG_IGN);
	if (!user_dir) {
		tmpdir = g_dir_make_tmp("xmpp2-harness-XXXXXX", NULL);
		user_dir = tmpdir;
	}
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
	purple_debug_set_ui_ops(&debug_ops);
	purple_debug_set_enabled(debug_on);
	purple_eventloop_set_ui_ops(&eventloop_ops);
	if (plugin_dir)
		purple_plugins_add_search_path(plugin_dir);
	if (!purple_core_init(UI_ID)) {
		fprintf(stderr, "libpurple initialization failed\n");
		return 2;
	}
	purple_set_blist(purple_blist_new());
	purple_prefs_load();

	if (!purple_find_prpl(prpl)) {
		GList *l;

		fprintf(stderr, "protocol %s not found; available:", prpl);
		for (l = purple_plugins_get_protocols(); l; l = l->next)
			fprintf(stderr, " %s", ((PurplePlugin *)l->data)->info->id);
		fprintf(stderr, "\n");
		return 2;
	}

	account = purple_account_new(user, prpl);
	purple_account_set_password(account, pass);
	purple_account_set_remember_password(account, FALSE);
	/* Self-signed test cert is trusted via --trust-cert; plain auth is allowed. */
	purple_account_set_string(account, "connection_security", "opportunistic_tls");
	purple_account_set_bool(account, "auth_plain_in_clear", TRUE);
	if (host)
		purple_account_set_string(account, "connect_server", host);
	if (port)
		purple_account_set_int(account, "port", port);
	for (i = 0; i < (int)prefs->len; i++)
		set_pref(account, g_ptr_array_index(prefs, i));
	purple_accounts_add(account);

	purple_signal_connect(purple_conversations_get_handle(), "wrote-im-msg",
	                      &sig_handle, PURPLE_CALLBACK(on_wrote_im), NULL);
	purple_signal_connect(purple_connections_get_handle(), "signed-on",
	                      &sig_handle, PURPLE_CALLBACK(on_signed_on), NULL);
	purple_signal_connect(purple_connections_get_handle(), "signed-off",
	                      &sig_handle, PURPLE_CALLBACK(on_signed_off), NULL);
	purple_signal_connect(purple_connections_get_handle(), "connection-error",
	                      &sig_handle, PURPLE_CALLBACK(on_conn_error), NULL);

	script = g_queue_new();
	received = g_ptr_array_new();
	load_script(script_path);

	loop = g_main_loop_new(NULL, FALSE);
	g_idle_add(next_command, NULL);
	g_main_loop_run(loop);

	purple_account_set_enabled(account, UI_ID, FALSE);
	purple_core_quit();
	if (tmpdir)
		remove_tree(tmpdir);
	return exit_code;
}
