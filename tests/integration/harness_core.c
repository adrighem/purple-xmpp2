/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */

/* Run state: pending assertion, completion, timers, polling, signals. */
#include <stdlib.h>
#include <string.h>

#include "harness.h"

FILE *out;
const char *userdir_path;
int c2s_port;

GMainLoop *loop;
PurpleAccount *account;
GQueue *script;        /* of char* */
Pending pending = P_NONE;
char *pending_cmd;
char *pending_text;    /* join: room */
Filter *pending_filter;
gboolean pending_next, pending_fail_ok;
static guint pending_timer;
static guint poll_source;
static gboolean (*poll_fn)(char **detail);
int exit_code;


void finish(int code)
{
	exit_code = code;
	g_main_loop_quit(loop);
}

void say(gboolean ok, const char *cmd, const char *detail)
{
	fprintf(out, "%s %s%s%s\n", ok ? "OK" : "FAIL", cmd, detail ? ": " : "", detail ? detail : "");
	fflush(out);
}

void resume(gboolean ok, const char *detail)
{
	if (pending_timer)
		g_source_remove(pending_timer);
	pending_timer = 0;
	if (poll_source)
		g_source_remove(poll_source);
	poll_source = 0;
	poll_fn = NULL;
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

void fail_now(const char *cmd, const char *detail)
{
	say(FALSE, cmd, detail);
	finish(1);
}

void continue_ok(const char *cmd, const char *detail)
{
	say(TRUE, cmd, detail);
	g_idle_add(next_command, NULL);
}

/* Re-evaluate the armed assertion after new data arrived. */
void poll_pending(void)
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
	case P_POLL_ABSENT:
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

void arm(Pending p, const char *cmd, double secs)
{
	pending = p;
	g_free(pending_cmd);
	pending_cmd = g_strdup(cmd);
	pending_timer = g_timeout_add((guint)(secs * 1000), on_timeout, NULL);
}
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
PurpleConversation *find_conv(const char *name)
{
	PurpleConversation *c = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, name, account);

	if (!c)
		c = purple_find_conversation_with_account(PURPLE_CONV_TYPE_CHAT, name, account);
	return c;
}

gboolean need_online(const char *cmd)
{
	if (purple_account_is_connected(account))
		return TRUE;
	fail_now(cmd, "account is not connected");
	return FALSE;
}

/* Split "A B REST" into first token and rest; modifies in place. */
char *split_word(char *s)
{
	char *sp = strchr(s, ' ');

	if (!sp)
		return (char *)"";
	*sp++ = '\0';
	return g_strstrip(sp);
}

static gboolean poll_tick(gpointer unused)
{
	char *detail = NULL;
	gboolean hit = poll_fn(&detail);

	if (!hit)
		return TRUE;
	poll_source = 0;
	if (pending == P_POLL_ABSENT)
		resume(FALSE, "unexpected request or state");
	else
		resume(TRUE, detail);
	g_free(detail);
	return FALSE;
}

/* Polls fn every 100 ms (and once now) until it returns TRUE. */
void poll_arm(Pending p, const char *cmd, double secs,
                     gboolean (*fn)(char **detail))
{
	arm(p, cmd, secs);
	poll_fn = fn;
	if (poll_tick(NULL))
		poll_source = g_timeout_add(100, poll_tick, NULL);
}


/* Hooks the connection, chat and file transfer signals; call after core init. */
void harness_connect_signals(void)
{
	static int sig_handle;

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
}
