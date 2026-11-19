/*
 * xmpp2-harness: headless libpurple 2.x client for integration tests.
 * Derived from libpurple/example/nullclient.c (GPLv2+, Pidgin developers).
 *
 * Usage:
 *   xmpp2-harness --prpl prpl-xmpp2 --plugin-dir build \
 *       --user alice@localhost/harness --password secret \
 *       --host 127.0.0.1 --port 15222 [--pref s:name=val|i:name=1|b:name=1]
 *       [--script FILE] [--userdir DIR] [--log txt] [--trust-cert FILE] [--debug]
 *
 *   --userdir DIR     persistent libpurple user dir (state, logs, accounts and
 *                     blist survive restarts). Default: a temp dir removed on exit.
 *                     (--user-dir is accepted as an alias.)
 *   --log txt         enable IM and chat logging in text format.
 *   --trust-cert      pre-seed the tls_peers pool with a PEM for the account
 *                     domain and --host, so a self-signed cert is accepted.
 *   --debug           copy libpurple debug to stderr (always captured internally).
 *
 * Commands (one per line, from --script or stdin; '#' starts a comment).
 * Each prints "OK ..." or "FAIL ..."; the first FAIL ends the run with exit 1
 * (2 on setup errors).
 *
 * Session:
 *   connect [SECS]            enable the account, wait for sign-on (20)
 *   disconnect                disable the account and wait for sign-off
 *   sleep SECS
 *   pref s:NAME=V|i:NAME=N|b:NAME=0|1   set an account pref now
 *   buddy-add JID             add to the roster and subscribe
 *   quit                      stop (implicit at end of input)
 * Actions:
 *   send JID TEXT             send an IM
 *   join ROOM NICK            join a MUC, wait for chat-joined (20)
 *   leave ROOM                leave a MUC
 *   groupsend ROOM TEXT       send a groupchat message
 *   cmd TARGET TEXT           run a slash command in the conversation (no '/')
 *   correct JID TEXT          shorthand for: cmd JID correct TEXT
 *   upload JID PATH [SECS]    send a file, wait for xfer completion (30)
 *   upload-fail JID PATH [SECS]  same, but the xfer must be cancelled
 *   focus JID                 unseen-count := 0 and emit conversation-updated
 *                             (PURPLE_CONV_UPDATE_UNSEEN), like a focused window
 * Assertions on conversation events (EV lines):
 *   expect-ev FILTER          wait until an event matches (history counts)
 *   expect-next FILTER        same, but only events after the previous
 *                             expect-next match (ordered checks)
 *   no-ev FILTER              no match in history or during timeout (3)
 *   count-ev FILTER n=N       exactly N matches so far
 *   clear-ev                  forget events and reset the expect-next cursor
 *   logcount JID TEXT N       TEXT occurs exactly N times in <userdir>/logs for JID
 *   wait TEXT [SECS]          legacy: expect-ev kind=recv text=TEXT
 *   absent TEXT [SECS]        legacy: no-ev kind=recv text=TEXT
 *   count TEXT N              legacy: count-ev kind=recv n=N text=TEXT
 * Assertions on libpurple debug (categories starting with "xmpp2" only):
 *   expect-debug|expect-debug-next|no-debug|count-debug|clear-debug FILTER
 *   dump-debug                print captured xmpp2 debug lines
 *
 * FILTER = space separated key=value, optional trailing text:
 *   kind=recv|sent|system|error|other   conv=NAME  who=JIDPREFIX  ctype=im|chat
 *   flag=A|B (all set)  noflag=A|B (none set)  mtime=EPOCH,SECS  timeout=SECS  n=N
 *   cat=xmpp2-prefix (debug only)
 *   text=SUBSTRING | texteq=EXACT   (must be last; takes the rest of the line)
 * Flag names: SEND RECV SYSTEM AUTO_RESP ACTIVE_ONLY NICK NO_LOG WHISPER ERROR
 * DELAYED RAW IMAGES NOTIFY NO_LINKIFY INVISIBLE REMOTE_SEND.
 *
 * Event line, one per conversation write (incoming, sent, system):
 *   EV <kind> conv=<name> who=<jid|-> flags=<hex> mtime=<epoch> names=<A,B> ctype=<im|chat> <text>
 * Newlines in <text> are escaped as \n.
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

/* ---- flags ---- */
static const struct { const char *name; guint bit; } flag_names[] = {
	{ "SEND", PURPLE_MESSAGE_SEND }, { "RECV", PURPLE_MESSAGE_RECV },
	{ "SYSTEM", PURPLE_MESSAGE_SYSTEM }, { "AUTO_RESP", PURPLE_MESSAGE_AUTO_RESP },
	{ "ACTIVE_ONLY", PURPLE_MESSAGE_ACTIVE_ONLY }, { "NICK", PURPLE_MESSAGE_NICK },
	{ "NO_LOG", PURPLE_MESSAGE_NO_LOG }, { "WHISPER", PURPLE_MESSAGE_WHISPER },
	{ "ERROR", PURPLE_MESSAGE_ERROR }, { "DELAYED", PURPLE_MESSAGE_DELAYED },
	{ "RAW", PURPLE_MESSAGE_RAW }, { "IMAGES", PURPLE_MESSAGE_IMAGES },
	{ "NOTIFY", PURPLE_MESSAGE_NOTIFY }, { "NO_LINKIFY", PURPLE_MESSAGE_NO_LINKIFY },
	{ "INVISIBLE", PURPLE_MESSAGE_INVISIBLE },
	{ "REMOTE_SEND", PURPLE_MESSAGE_REMOTE_SEND },
};

static guint parse_flags(const char *spec, gboolean *ok)
{
	char **parts = g_strsplit_set(spec, "|,", -1);
	guint bits = 0;
	int i;
	gsize k;

	for (i = 0; parts[i]; i++) {
		gboolean found = FALSE;

		for (k = 0; k < G_N_ELEMENTS(flag_names); k++) {
			if (!strcmp(parts[i], flag_names[k].name)) {
				bits |= flag_names[k].bit;
				found = TRUE;
			}
		}
		if (!found && *parts[i])
			*ok = FALSE;
	}
	g_strfreev(parts);
	return bits;
}

static char *flag_list(guint flags)
{
	GString *out = g_string_new(NULL);
	gsize k;

	for (k = 0; k < G_N_ELEMENTS(flag_names); k++) {
		if (flags & flag_names[k].bit)
			g_string_append_printf(out, "%s%s", out->len ? "," : "", flag_names[k].name);
	}
	if (!out->len)
		g_string_append_c(out, '-');
	return g_string_free(out, FALSE);
}

/* ---- captured data ---- */
typedef struct {
	const char *kind;     /* static string */
	char *conv, *who, *text;
	guint flags;
	time_t mtime;
	gboolean chat;
} Ev;

typedef struct {
	char *cat, *text;
} Dbg;

static GPtrArray *evs;    /* of Ev* */
static GPtrArray *dbgs;   /* of Dbg* */
static guint ev_cursor, dbg_cursor;
static gboolean debug_echo;
static FILE *out; /* real stdout; libpurple prints stray lines to fd 1 */

static void poll_pending(void);

static void debug_print(PurpleDebugLevel level, const char *category, const char *arg)
{
	Dbg *d;

	if (debug_echo)
		fprintf(stderr, "DEBUG[%d] %s: %s", (int)level, category ? category : "", arg);
	if (!category || strncmp(category, "xmpp2", 5) != 0)
		return;
	d = g_new0(Dbg, 1);
	d->cat = g_strdup(category);
	d->text = g_strdup(arg);
	g_ptr_array_add(dbgs, d);
	poll_pending();
}

static gboolean debug_enabled(PurpleDebugLevel level, const char *category)
{
	(void)level;
	(void)category;
	return TRUE;
}

static PurpleDebugUiOps debug_ops = { debug_print, debug_enabled, NULL, NULL, NULL, NULL };

/* ---- filters ---- */
typedef struct {
	char *kind, *conv, *who, *ctype, *cat, *text, *texteq;
	guint flag_req, flag_no;
	time_t mt_epoch;
	int mt_secs;
	double timeout;
	int n;
} Filter;

static void filter_free(Filter *f)
{
	if (!f)
		return;
	g_free(f->kind); g_free(f->conv); g_free(f->who); g_free(f->ctype);
	g_free(f->cat); g_free(f->text); g_free(f->texteq);
	g_free(f);
}

/* Parse "k=v k=v ... [text=REST]"; NULL and *err on a bad token. */
static Filter *filter_parse(const char *args, double default_timeout, char **err)
{
	Filter *f = g_new0(Filter, 1);
	const char *p = args;
	gboolean ok = TRUE;

	f->timeout = default_timeout;
	f->n = -1;
	while (*p) {
		const char *end;
		char *tok, *eq;

		while (*p == ' ')
			p++;
		if (!*p)
			break;
		if (g_str_has_prefix(p, "text=")) {
			f->text = g_strdup(p + 5);
			break;
		}
		if (g_str_has_prefix(p, "texteq=")) {
			f->texteq = g_strdup(p + 7);
			break;
		}
		end = strchr(p, ' ');
		tok = end ? g_strndup(p, end - p) : g_strdup(p);
		p = end ? end : p + strlen(p);
		eq = strchr(tok, '=');
		if (!eq) {
			*err = g_strdup_printf("bad filter token '%s'", tok);
			g_free(tok);
			filter_free(f);
			return NULL;
		}
		*eq++ = '\0';
		if (!strcmp(tok, "kind")) f->kind = g_strdup(eq);
		else if (!strcmp(tok, "conv")) f->conv = g_strdup(eq);
		else if (!strcmp(tok, "who")) f->who = g_strdup(eq);
		else if (!strcmp(tok, "ctype")) f->ctype = g_strdup(eq);
		else if (!strcmp(tok, "cat")) f->cat = g_strdup(eq);
		else if (!strcmp(tok, "flag")) f->flag_req = parse_flags(eq, &ok);
		else if (!strcmp(tok, "noflag")) f->flag_no = parse_flags(eq, &ok);
		else if (!strcmp(tok, "timeout")) f->timeout = atof(eq);
		else if (!strcmp(tok, "n")) f->n = atoi(eq);
		else if (!strcmp(tok, "mtime")) {
			f->mt_epoch = (time_t)atoll(eq);
			f->mt_secs = strchr(eq, ',') ? atoi(strchr(eq, ',') + 1) : 2;
		} else {
			*err = g_strdup_printf("unknown filter key '%s'", tok);
			ok = FALSE;
		}
		g_free(tok);
		if (!ok) {
			if (!*err)
				*err = g_strdup("unknown flag name");
			filter_free(f);
			return NULL;
		}
	}
	return f;
}

/* A filter without a resource matches the conversation name with or without one
 * (libpurple may name an IM conversation after the sender's full JID). */
static gboolean conv_matches(const char *want, const char *have)
{
	const char *slash = strchr(have, '/');

	if (!strcmp(want, have))
		return TRUE;
	return !strchr(want, '/') && slash && strlen(want) == (gsize)(slash - have) &&
	       !strncmp(want, have, slash - have);
}

static gboolean ev_matches(const Filter *f, const Ev *e)
{
	if (f->kind && strcmp(f->kind, e->kind))
		return FALSE;
	if (f->conv && !conv_matches(f->conv, e->conv))
		return FALSE;
	if (f->who && !g_str_has_prefix(e->who, f->who))
		return FALSE;
	if (f->ctype && strcmp(f->ctype, e->chat ? "chat" : "im"))
		return FALSE;
	if ((e->flags & f->flag_req) != f->flag_req || (e->flags & f->flag_no))
		return FALSE;
	if (f->mt_epoch && labs((long)(e->mtime - f->mt_epoch)) > f->mt_secs)
		return FALSE;
	if (f->text && !strstr(e->text, f->text))
		return FALSE;
	if (f->texteq && strcmp(e->text, f->texteq))
		return FALSE;
	return TRUE;
}

static gboolean dbg_matches(const Filter *f, const Dbg *d)
{
	if (f->cat && !g_str_has_prefix(d->cat, f->cat))
		return FALSE;
	if (f->text && !strstr(d->text, f->text))
		return FALSE;
	if (f->texteq && strcmp(d->text, f->texteq))
		return FALSE;
	return TRUE;
}

static int ev_find(const Filter *f, guint from)
{
	guint i;

	for (i = from; i < evs->len; i++)
		if (ev_matches(f, g_ptr_array_index(evs, i)))
			return (int)i;
	return -1;
}

static int dbg_find(const Filter *f, guint from)
{
	guint i;

	for (i = from; i < dbgs->len; i++)
		if (dbg_matches(f, g_ptr_array_index(dbgs, i)))
			return (int)i;
	return -1;
}

static int ev_count(const Filter *f)
{
	guint i;
	int n = 0;

	for (i = 0; i < evs->len; i++)
		n += ev_matches(f, g_ptr_array_index(evs, i));
	return n;
}

static int dbg_count(const Filter *f)
{
	guint i;
	int n = 0;

	for (i = 0; i < dbgs->len; i++)
		n += dbg_matches(f, g_ptr_array_index(dbgs, i));
	return n;
}

/* ---- run state ---- */
typedef enum {
	P_NONE, P_CONNECT, P_DISCONNECT, P_SLEEP, P_JOIN, P_XFER,
	P_EXPECT_EV, P_ABSENT_EV, P_EXPECT_DBG, P_ABSENT_DBG
} Pending;

static GMainLoop *loop;
static PurpleAccount *account;
static GQueue *script;        /* of char* */
static Pending pending = P_NONE;
static char *pending_cmd;
static char *pending_text;    /* join: room */
static Filter *pending_filter;
static gboolean pending_next, pending_fail_ok;
static guint pending_timer;
static int exit_code;

static gboolean next_command(gpointer unused);

static void finish(int code)
{
	exit_code = code;
	g_main_loop_quit(loop);
}

static void say(gboolean ok, const char *cmd, const char *detail)
{
	fprintf(out, "%s %s%s%s\n", ok ? "OK" : "FAIL", cmd, detail ? ": " : "", detail ? detail : "");
	fflush(out);
}

static void resume(gboolean ok, const char *detail)
{
	if (pending_timer)
		g_source_remove(pending_timer);
	pending_timer = 0;
	pending = P_NONE;
	say(ok, pending_cmd, detail);
	filter_free(pending_filter);
	pending_filter = NULL;
	g_free(pending_text);
	pending_text = NULL;
	if (!ok)
		finish(1);
	else
		g_idle_add(next_command, NULL);
}

static void fail_now(const char *cmd, const char *detail)
{
	say(FALSE, cmd, detail);
	finish(1);
}

static void continue_ok(const char *cmd, const char *detail)
{
	say(TRUE, cmd, detail);
	g_idle_add(next_command, NULL);
}

/* Re-evaluate the armed assertion after new data arrived. */
static void poll_pending(void)
{
	int idx;

	switch (pending) {
	case P_EXPECT_EV:
		idx = ev_find(pending_filter, pending_next ? ev_cursor : 0);
		if (idx >= 0) {
			if (pending_next)
				ev_cursor = idx + 1;
			resume(TRUE, ((Ev *)g_ptr_array_index(evs, idx))->text);
		}
		break;
	case P_ABSENT_EV:
		if (ev_find(pending_filter, 0) >= 0)
			resume(FALSE, "unexpected event seen");
		break;
	case P_EXPECT_DBG:
		idx = dbg_find(pending_filter, pending_next ? dbg_cursor : 0);
		if (idx >= 0) {
			if (pending_next)
				dbg_cursor = idx + 1;
			resume(TRUE, NULL);
		}
		break;
	case P_ABSENT_DBG:
		if (dbg_find(pending_filter, 0) >= 0)
			resume(FALSE, "unexpected debug line seen");
		break;
	default:
		break;
	}
}

static gboolean on_timeout(gpointer data)
{
	(void)data;
	pending_timer = 0;
	switch (pending) {
	case P_ABSENT_EV:
	case P_ABSENT_DBG:
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

static void arm(Pending p, const char *cmd, double secs)
{
	pending = p;
	g_free(pending_cmd);
	pending_cmd = g_strdup(cmd);
	pending_timer = g_timeout_add((guint)(secs * 1000), on_timeout, NULL);
}

/* ---- conversation capture ---- */
static const char *kind_of(guint flags)
{
	if (flags & PURPLE_MESSAGE_SYSTEM)
		return "system";
	if (flags & PURPLE_MESSAGE_ERROR)
		return "error";
	if (flags & PURPLE_MESSAGE_SEND)
		return "sent";
	if (flags & PURPLE_MESSAGE_RECV)
		return "recv";
	return "other";
}

static void null_write_conv(PurpleConversation *conv, const char *who, const char *alias,
                            const char *message, PurpleMessageFlags flags, time_t mtime)
{
	Ev *e;
	char *plain, *names, *esc, *p;

	(void)alias;
	if (purple_conversation_get_account(conv) != account || !message)
		return;
	plain = purple_markup_strip_html(message);
	e = g_new0(Ev, 1);
	e->kind = kind_of(flags);
	e->conv = g_strdup(purple_conversation_get_name(conv));
	e->who = g_strdup(who && *who ? who : "-");
	e->text = plain;
	e->flags = flags;
	e->mtime = mtime;
	e->chat = purple_conversation_get_type(conv) == PURPLE_CONV_TYPE_CHAT;
	g_ptr_array_add(evs, e);

	names = flag_list(flags);
	esc = g_strdup(plain);
	while ((p = strchr(esc, '\n')))
		*p = ' ';
	fprintf(out, "EV %s conv=%s who=%s flags=0x%x mtime=%ld names=%s ctype=%s %s\n", e->kind,
	       e->conv, e->who, (unsigned)flags, (long)mtime, names, e->chat ? "chat" : "im", esc);
	fflush(out);
	g_free(names);
	g_free(esc);

	if (!e->chat && (flags & PURPLE_MESSAGE_RECV) && !(flags & PURPLE_MESSAGE_DELAYED)) {
		/* Mimic Pidgin: an unfocused conversation accumulates unseen messages. */
		gint n = GPOINTER_TO_INT(purple_conversation_get_data(conv, "unseen-count"));

		purple_conversation_set_data(conv, "unseen-count", GINT_TO_POINTER(n + 1));
		purple_conversation_update(conv, PURPLE_CONV_UPDATE_UNSEEN);
	}
	poll_pending();
}

static PurpleConversationUiOps conv_ops = {
	NULL, NULL, NULL, NULL, null_write_conv, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

/* ---- signals ---- */
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

static void on_conn_error(PurpleConnection *gc, PurpleConnectionError err, const char *desc)
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

static void on_chat_joined(PurpleConversation *conv)
{
	if (pending == P_JOIN && pending_text &&
	    !g_ascii_strcasecmp(purple_conversation_get_name(conv), pending_text))
		resume(TRUE, pending_text);
}

static void on_xfer_done(PurpleXfer *xfer, gboolean ok)
{
	if (pending != P_XFER)
		return;
	fprintf(out, "EV xfer conv=%s who=%s flags=0x0 mtime=%ld names=- ctype=im %s\n",
	       purple_xfer_get_remote_user(xfer), purple_xfer_get_remote_user(xfer),
	       (long)time(NULL), ok ? "complete" : "cancelled");
	if (ok == !pending_fail_ok)
		resume(TRUE, ok ? "xfer complete" : "xfer cancelled as expected");
	else
		resume(FALSE, ok ? "xfer completed but failure expected" : "xfer cancelled");
}

static void on_send_complete(PurpleXfer *xfer) { on_xfer_done(xfer, TRUE); }
static void on_send_cancel(PurpleXfer *xfer) { on_xfer_done(xfer, FALSE); }

/* ---- command helpers ---- */
static PurpleConversation *find_conv(const char *name)
{
	PurpleConversation *c = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, name, account);

	if (!c)
		c = purple_find_conversation_with_account(PURPLE_CONV_TYPE_CHAT, name, account);
	return c;
}

static int logcount_dir(const char *dir, const char *text)
{
	GDir *d = g_dir_open(dir, 0, NULL);
	const char *name;
	int n = 0;

	if (!d)
		return 0;
	while ((name = g_dir_read_name(d))) {
		char *path = g_build_filename(dir, name, NULL), *data = NULL;
		const char *p;

		if (g_file_get_contents(path, &data, NULL, NULL)) {
			for (p = strstr(data, text); p; p = strstr(p + 1, text))
				n++;
			g_free(data);
		}
		g_free(path);
	}
	g_dir_close(d);
	return n;
}

/* logs/PROTO/ACCOUNT/ESCAPED_JID/DATE.txt */
static int logcount(const char *userdir, const char *jid, const char *text)
{
	char *root = g_build_filename(userdir, "logs", NULL);
	char *esc = g_strdup(purple_escape_filename(jid));
	GDir *protos = g_dir_open(root, 0, NULL);
	const char *pn;
	int n = 0;

	while (protos && (pn = g_dir_read_name(protos))) {
		char *pdir = g_build_filename(root, pn, NULL);
		GDir *accts = g_dir_open(pdir, 0, NULL);
		const char *an;

		while (accts && (an = g_dir_read_name(accts))) {
			char *cdir = g_build_filename(pdir, an, esc, NULL);

			n += logcount_dir(cdir, text);
			g_free(cdir);
		}
		if (accts)
			g_dir_close(accts);
		g_free(pdir);
	}
	if (protos)
		g_dir_close(protos);
	g_free(root);
	g_free(esc);
	return n;
}

static const char *userdir_path;

static void set_pref(PurpleAccount *acct, const char *spec)
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

static gboolean need_online(const char *cmd)
{
	if (purple_account_is_connected(account))
		return TRUE;
	fail_now(cmd, "account is not connected");
	return FALSE;
}

/* Split "A B REST" into first token and rest; modifies in place. */
static char *split_word(char *s)
{
	char *sp = strchr(s, ' ');

	if (!sp)
		return (char *)"";
	*sp++ = '\0';
	return g_strstrip(sp);
}

/* Shared body of expect-ev/expect-next/no-ev/count-ev and the debug twins. */
static void assertion(const char *cmd, char *arg)
{
	gboolean dbg = g_str_has_suffix(cmd, "debug") || g_str_has_suffix(cmd, "debug-next");
	gboolean absent = g_str_has_prefix(cmd, "no-");
	gboolean count = g_str_has_prefix(cmd, "count-");
	gboolean next = g_str_has_suffix(cmd, "-next");
	char *err = NULL;
	Filter *f = filter_parse(arg, absent ? 3 : 20, &err);
	int got;

	if (!f) {
		fail_now(cmd, err);
		g_free(err);
		return;
	}
	if (dbg && f->cat && strncmp(f->cat, "xmpp2", 5) != 0) {
		fail_now(cmd, "only xmpp2* debug categories may be asserted");
		filter_free(f);
		return;
	}
	if (dbg && !f->cat)
		f->cat = g_strdup("xmpp2");
	if (count) {
		got = dbg ? dbg_count(f) : ev_count(f);
		if (f->n < 0) {
			fail_now(cmd, "count needs n=N");
		} else if (got != f->n) {
			char *m = g_strdup_printf("want %d got %d", f->n, got);

			fail_now(cmd, m);
			g_free(m);
		} else {
			continue_ok(cmd, NULL);
		}
		filter_free(f);
		return;
	}
	pending_filter = f;
	pending_next = next;
	arm(absent ? (dbg ? P_ABSENT_DBG : P_ABSENT_EV) : (dbg ? P_EXPECT_DBG : P_EXPECT_EV), cmd,
	    f->timeout);
	poll_pending();
}

static void legacy_assertion(const char *cmd, char *arg)
{
	gboolean w = !strcmp(cmd, "wait");
	char *sp = strrchr(arg, ' ');
	double secs = w ? 20 : 3;
	char *line;

	if (!strcmp(cmd, "count")) {
		if (!sp) {
			fail_now(cmd, "usage 'count TEXT N'");
			return;
		}
		line = g_strdup_printf("kind=recv n=%d text=%.*s", atoi(sp + 1), (int)(sp - arg), arg);
		assertion("count-ev", line);
		g_free(line);
		return;
	}
	if (sp && g_ascii_isdigit(sp[1]) && atof(sp + 1) > 0) {
		secs = atof(sp + 1);
		*sp = '\0';
	}
	line = g_strdup_printf("kind=recv timeout=%g text=%s", secs, arg);
	assertion(w ? "expect-ev" : "no-ev", line);
	g_free(line);
}

static void do_command(char *line)
{
	char *cmd = g_strstrip(line);
	char *arg = split_word(cmd);
	double secs;

	if (!*cmd || *cmd == '#') {
		g_idle_add(next_command, NULL);
		return;
	}
	if (pending_cmd)
		g_free(pending_cmd), pending_cmd = NULL;

	if (!strcmp(cmd, "connect")) {
		secs = *arg ? atof(arg) : 20;
		arm(P_CONNECT, "connect", secs);
		purple_account_set_enabled(account, UI_ID, TRUE);
		purple_savedstatus_activate(purple_savedstatus_new(NULL, PURPLE_STATUS_AVAILABLE));
		if (purple_account_is_connected(account))
			resume(TRUE, NULL);
	} else if (!strcmp(cmd, "disconnect")) {
		arm(P_DISCONNECT, "disconnect", 10);
		if (!purple_account_is_connected(account)) {
			resume(TRUE, "already offline");
			return;
		}
		purple_account_set_enabled(account, UI_ID, FALSE);
		purple_account_disconnect(account);
	} else if (!strcmp(cmd, "sleep")) {
		arm(P_SLEEP, "sleep", atof(arg));
	} else if (!strcmp(cmd, "quit")) {
		finish(0);
	} else if (!strcmp(cmd, "pref")) {
		set_pref(account, arg);
		continue_ok("pref", arg);
	} else if (!strcmp(cmd, "buddy-add")) {
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
	} else if (!strcmp(cmd, "send")) {
		char *text = split_word(arg);
		PurpleConversation *conv;

		if (!*text || !need_online("send"))
			return;
		conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, arg);
		purple_conv_im_send(purple_conversation_get_im_data(conv), text);
		continue_ok("send", arg);
	} else if (!strcmp(cmd, "join")) {
		char *nick = split_word(arg);
		PurplePluginProtocolInfo *info;
		GHashTable *comps;

		if (!*nick || !need_online("join"))
			return;
		info = PURPLE_PLUGIN_PROTOCOL_INFO(purple_find_prpl(purple_account_get_protocol_id(account)));
		if (!info->chat_info_defaults) {
			fail_now("join", "protocol has no chat_info_defaults");
			return;
		}
		comps = info->chat_info_defaults(purple_account_get_connection(account), arg);
		g_hash_table_replace(comps, g_strdup("handle"), g_strdup(nick));
		pending_text = g_strdup(arg);
		arm(P_JOIN, "join", 20);
		serv_join_chat(purple_account_get_connection(account), comps);
		g_hash_table_destroy(comps);
	} else if (!strcmp(cmd, "leave")) {
		PurpleConversation *c = find_conv(arg);

		if (!c) {
			fail_now("leave", "no such conversation");
			return;
		}
		purple_conversation_destroy(c);
		continue_ok("leave", arg);
	} else if (!strcmp(cmd, "groupsend")) {
		char *text = split_word(arg);
		PurpleConversation *c = find_conv(arg);

		if (!c || purple_conversation_get_type(c) != PURPLE_CONV_TYPE_CHAT || !*text) {
			fail_now("groupsend", "no such room or empty text");
			return;
		}
		purple_conv_chat_send(purple_conversation_get_chat_data(c), text);
		continue_ok("groupsend", arg);
	} else if (!strcmp(cmd, "cmd") || !strcmp(cmd, "correct")) {
		gboolean is_correct = !strcmp(cmd, "correct");
		char *text = split_word(arg);
		PurpleConversation *c = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, arg, account);
		char *full, *error = NULL;
		PurpleCmdStatus st;

		if (!c)
			c = find_conv(arg);
		if (!c)
			c = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, arg);
		full = is_correct ? g_strdup_printf("correct %s", text) : g_strdup(text);
		st = purple_cmd_do_command(c, full, full, &error);
		g_free(full);
		if (st != PURPLE_CMD_STATUS_OK) {
			char *m = g_strdup_printf("command status %d %s", (int)st, error ? error : "");

			fail_now(cmd, m);
			g_free(m);
		} else {
			continue_ok(cmd, arg);
		}
		g_free(error);
	} else if (!strcmp(cmd, "focus")) {
		PurpleConversation *c = find_conv(arg);

		if (!c) {
			fail_now("focus", "no such conversation");
			return;
		}
		purple_conversation_set_data(c, "unseen-count", GINT_TO_POINTER(0));
		purple_conversation_update(c, PURPLE_CONV_UPDATE_UNSEEN);
		continue_ok("focus", arg);
	} else if (!strcmp(cmd, "upload") || !strcmp(cmd, "upload-fail")) {
		char *rest = split_word(arg);
		char *sp = strchr(rest, ' ');

		secs = 30;
		if (sp) {
			*sp = '\0';
			secs = atof(sp + 1);
		}
		if (!*rest || !need_online(cmd))
			return;
		pending_fail_ok = !strcmp(cmd, "upload-fail");
		arm(P_XFER, cmd, secs);
		serv_send_file(purple_account_get_connection(account), arg, rest);
	} else if (!strcmp(cmd, "logcount")) {
		char *text = split_word(arg);
		char *sp = strrchr(text, ' ');
		int want, got;

		if (!sp || !userdir_path) {
			fail_now("logcount", "usage 'logcount JID TEXT N' (needs --userdir)");
			return;
		}
		want = atoi(sp + 1);
		*sp = '\0';
		got = logcount(userdir_path, arg, text);
		if (got == want) {
			continue_ok("logcount", NULL);
		} else {
			char *m = g_strdup_printf("'%s' in logs of %s: want %d got %d", text, arg, want, got);

			fail_now("logcount", m);
			g_free(m);
		}
	} else if (!strcmp(cmd, "clear-ev")) {
		g_ptr_array_set_size(evs, 0);
		ev_cursor = 0;
		continue_ok("clear-ev", NULL);
	} else if (!strcmp(cmd, "clear-debug")) {
		g_ptr_array_set_size(dbgs, 0);
		dbg_cursor = 0;
		continue_ok("clear-debug", NULL);
	} else if (!strcmp(cmd, "dump-debug")) {
		guint i;

		for (i = 0; i < dbgs->len; i++) {
			Dbg *d = g_ptr_array_index(dbgs, i);

			fprintf(out, "DBG %s: %s%s", d->cat, d->text, g_str_has_suffix(d->text, "\n") ? "" : "\n");
		}
		continue_ok("dump-debug", NULL);
	} else if (g_str_has_prefix(cmd, "expect-") || g_str_has_prefix(cmd, "no-") ||
	           g_str_has_prefix(cmd, "count-")) {
		assertion(cmd, arg);
	} else if (!strcmp(cmd, "wait") || !strcmp(cmd, "absent") || !strcmp(cmd, "count")) {
		legacy_assertion(cmd, arg);
	} else {
		fail_now(cmd, "unknown command");
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

int main(int argc, char **argv)
{
	const char *prpl = "prpl-jabber", *user = NULL, *pass = getenv("XMPP_PASSWORD");
	const char *trust = NULL, *log_fmt = NULL;
	const char *host = NULL, *script_path = NULL, *plugin_dir = NULL, *user_dir = NULL;
	int port = 0, i;
	char *tmpdir = NULL;
	GPtrArray *prefs = g_ptr_array_new();
	static int sig_handle;

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];
		const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;

		if (!strcmp(a, "--debug")) {
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
		else { fprintf(stderr, "unknown option %s\n", a); return 2; }
	}
	if (!user || !pass) {
		fprintf(stderr, "--user and --password (or XMPP_PASSWORD) required\n");
		return 2;
	}

	signal(SIGCHLD, SIG_IGN);
	out = fdopen(dup(STDOUT_FILENO), "w");
	dup2(STDERR_FILENO, STDOUT_FILENO);
	evs = g_ptr_array_new();
	dbgs = g_ptr_array_new();
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
	purple_debug_set_ui_ops(&debug_ops);
	purple_debug_set_enabled(TRUE);
	purple_eventloop_set_ui_ops(&eventloop_ops);
	purple_conversations_set_ui_ops(&conv_ops);
	if (plugin_dir)
		purple_plugins_add_search_path(plugin_dir);
	if (!purple_core_init(UI_ID)) {
		fprintf(stderr, "libpurple initialization failed\n");
		return 2;
	}
	purple_set_blist(purple_blist_new());
	purple_blist_load();
	purple_prefs_load();
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
	for (i = 0; i < (int)prefs->len; i++)
		set_pref(account, g_ptr_array_index(prefs, i));

	purple_signal_connect(purple_connections_get_handle(), "signed-on", &sig_handle,
	                      PURPLE_CALLBACK(on_signed_on), NULL);
	purple_signal_connect(purple_connections_get_handle(), "signed-off", &sig_handle,
	                      PURPLE_CALLBACK(on_signed_off), NULL);
	purple_signal_connect(purple_connections_get_handle(), "connection-error", &sig_handle,
	                      PURPLE_CALLBACK(on_conn_error), NULL);
	purple_signal_connect(purple_conversations_get_handle(), "chat-joined", &sig_handle,
	                      PURPLE_CALLBACK(on_chat_joined), NULL);
	purple_signal_connect(purple_xfers_get_handle(), "file-send-complete", &sig_handle,
	                      PURPLE_CALLBACK(on_send_complete), NULL);
	purple_signal_connect(purple_xfers_get_handle(), "file-send-cancel", &sig_handle,
	                      PURPLE_CALLBACK(on_send_cancel), NULL);

	script = g_queue_new();
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
