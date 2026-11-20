/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */

/*
 * xmpp2-harness script language: command table, parser and dispatch.
 * Full reference below; the commands[] table drives --help.
 *
 * Commands (one per line, from --script or stdin; '#' starts a comment).
 * Each prints "OK ..." or "FAIL ..."; the first FAIL ends the run with exit 1
 * (2 on setup errors).
 *
 * Session:
 *   connect [SECS]            enable the account, wait for sign-on (20)
 *   disconnect                disable the account and wait for sign-off
 *   drop                      shutdown() every TCP socket of ours connected to
 *                             --port (no stream close, like a network drop);
 *                             the prpl then sees a connection error
 *   sleep SECS
 *   pref s:NAME=V|i:NAME=N|b:NAME=0|1   set an account pref now
 *   buddy-add JID             add to the roster and subscribe
 *   quit                      stop (implicit at end of input)
 *   remove-account            delete the account (purple_accounts_delete(),
 *                             as the Accounts dialog does) and stop
 * Actions:
 *   send JID TEXT             send an IM
 *   join ROOM NICK            join a MUC, wait for chat-joined (20)
 *   leave ROOM                leave a MUC
 *   groupsend ROOM TEXT       send a groupchat message
 *   cmd TARGET TEXT           run a slash command in the conversation (no '/')
 *   correct JID TEXT          shorthand for: cmd JID correct TEXT
 *   upload JID PATH [SECS]    send a file, wait for xfer completion (30)
 *   upload-fail JID PATH [SECS]  same, but the xfer must be cancelled
 *   xfer-cancel-at BYTES [cancel|disconnect|remove]
 *                             once the next transfer reported BYTES sent, from
 *                             the main loop: cancel it (like the transfer
 *                             window's Cancel button), sign the account off,
 *                             or delete the account; use with upload-fail
 *   rss-reset                 reset the peak RSS (VmHWM), remember the RSS
 *   rss-check DELTA_MB        FAIL if the peak RSS grew by more since rss-reset
 *                             (progress is printed as "EV xfer-progress
 *                             bytes=N size=M")
 *   upload-drop JID PATH [SECS]  start an upload and drop (as "drop") in
 *                             the same main loop turn, so the slot IQ is in
 *                             flight; the xfer must be cancelled
 *   raw XML                   send XML as is through the prpl's send_raw
 *                             (the XML console path); FAIL if refused
 *   raw-refused XML           same, but send_raw must refuse it
 *   focus JID                 unseen-count := 0 and emit conversation-updated
 *                             (PURPLE_CONV_UPDATE_UNSEEN), like a focused window
 *   action TEXT               run the prpl action of the (connected) account
 *                             whose label contains TEXT, like the Accounts menu
 *   plugin-action ID TEXT     run the action of the plugin ID (not a prpl, no
 *                             account needed) whose label contains TEXT, like
 *                             Tools > Plugin Actions; FAIL if the plugin is
 *                             not loaded or is a protocol plugin
 * Requests (purple_request_action() has a UI here that only records it):
 *   answer N [SECS]           wait for a request (10), press its button N
 *                             (0-based); prints "<primary> [b0|b1] -> bN"
 *   no-request [SECS]         no request arrives within SECS (3)
 * Accounts (all accounts of the userdir, not only --user):
 *   expect-account PRPL USER [exists=0|1] [enabled=0|1] [connected=0|1]
 *                             [timeout=SECS]   poll until it holds (10);
 *                             enabled is for this UI, USER is the full username
 *   --libpref s:|i:|b:/path=V  set a libpurple pref before the main loop
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
 *   kind=recv|sent|system|error|other|notify   conv=NAME  who=JIDPREFIX  ctype=im|chat
 *   (notify: purple_notify_message() dialogs, conv=- who=-)
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
#include <stdlib.h>
#include <string.h>

#include "harness.h"

/*
 * The command table. Adding a command is one line here plus its handler in a
 * harness_cmds_*.c file (and a prototype in harness.h). The help text is
 * "ARGS  description"; --help prints it for every row.
 */
static const Command commands[] = {
	/* session */
	{ "connect", cmd_connect, "[SECS]  enable the account, wait for sign-on (20)" },
	{ "disconnect", cmd_disconnect, "  disable the account and wait for sign-off" },
	{ "drop", cmd_drop, "  shutdown() every c2s TCP socket (a network drop, no stream close)" },
	{ "sleep", cmd_sleep, "SECS  wait" },
	{ "pref", cmd_pref, "s:NAME=V|i:NAME=N|b:NAME=0|1  set an account pref now" },
	{ "buddy-add", cmd_buddy_add, "JID  add to the roster and subscribe" },
	{ "quit", cmd_quit, "  stop (implicit at end of input)" },
	{ "remove-account", cmd_remove_account, "  delete the account, then stop" },
	{ "action", cmd_action, "TEXT  run the prpl action whose label contains TEXT" },
	{ "plugin-action", cmd_plugin_action, "ID TEXT  run the action of a standard plugin" },
	/* messages */
	{ "send", cmd_send, "JID TEXT  send an IM" },
	{ "cmd", cmd_cmd, "TARGET TEXT  run a slash command in the conversation (no '/')" },
	{ "correct", cmd_cmd, "JID TEXT  shorthand for: cmd JID correct TEXT" },
	{ "focus", cmd_focus, "JID  unseen-count := 0 and emit conversation-updated" },
	{ "raw", cmd_raw, "XML  send XML through send_raw; FAIL if refused" },
	{ "raw-refused", cmd_raw, "XML  same, but send_raw must refuse it" },
	/* rooms */
	{ "join", cmd_join, "ROOM NICK  join a MUC, wait for chat-joined (20)" },
	{ "leave", cmd_leave, "ROOM  leave a MUC" },
	{ "groupsend", cmd_groupsend, "ROOM TEXT  send a groupchat message" },
	/* files */
	{ "upload", cmd_upload, "JID PATH [SECS]  send a file, wait for completion (30)" },
	{ "upload-fail", cmd_upload, "JID PATH [SECS]  same, but the xfer must be cancelled" },
	{ "upload-drop", cmd_upload, "JID PATH [SECS]  start an upload and drop; must be cancelled" },
	{ "xfer-cancel-at", cmd_xfer_cancel_at,
	  "BYTES [cancel|disconnect|remove]  at BYTES sent, cancel the next transfer, sign off or delete the account" },
	{ "rss-reset", cmd_rss_reset, "  reset the peak RSS (VmHWM), remember the RSS" },
	{ "rss-check", cmd_rss_check, "DELTA_MB  FAIL if the peak RSS grew by more since rss-reset" },
	/* requests and accounts */
	{ "answer", cmd_answer, "N [SECS]  press button N of the next request (10)" },
	{ "no-request", cmd_no_request, "[SECS]  no request arrives within SECS (3)" },
	{ "expect-account", cmd_expect_account,
	  "PRPL USER [exists=0|1] [enabled=0|1] [connected=0|1] [timeout=SECS]  poll until it holds (10)" },
	/* EV assertions */
	{ "expect-ev", cmd_assert, "FILTER  wait until an event matches (history counts)" },
	{ "expect-next", cmd_assert, "FILTER  same, only after the previous expect-next match" },
	{ "no-ev", cmd_assert, "FILTER  no match in history or during timeout (3)" },
	{ "count-ev", cmd_assert, "FILTER n=N  exactly N matches so far" },
	{ "clear-ev", cmd_clear_ev, "  forget events and reset the expect-next cursor" },
	{ "logcount", cmd_logcount, "JID TEXT N  TEXT occurs exactly N times in <userdir>/logs for JID" },
	{ "wait", cmd_legacy, "TEXT [SECS]  legacy: expect-ev kind=recv text=TEXT" },
	{ "absent", cmd_legacy, "TEXT [SECS]  legacy: no-ev kind=recv text=TEXT" },
	{ "count", cmd_legacy, "TEXT N  legacy: count-ev kind=recv n=N text=TEXT" },
	/* debug assertions (xmpp2* categories only) */
	{ "expect-debug", cmd_assert, "FILTER  wait for a captured xmpp2 debug line" },
	{ "expect-debug-next", cmd_assert, "FILTER  same, ordered after the previous match" },
	{ "no-debug", cmd_assert, "FILTER  no matching debug line (3)" },
	{ "count-debug", cmd_assert, "FILTER n=N  exactly N matching debug lines" },
	{ "clear-debug", cmd_clear_debug, "  forget debug lines and reset the cursor" },
	{ "dump-debug", cmd_dump_debug, "  print captured xmpp2 debug lines" },
};

void harness_print_help(FILE *fp)
{
	gsize i;

	fprintf(fp, "Commands (one per line from --script or stdin; '#' starts a comment):\n");
	for (i = 0; i < G_N_ELEMENTS(commands); i++)
		fprintf(fp, "  %s %s\n", commands[i].name, commands[i].help);
	fprintf(fp, "\nFILTER = key=value pairs, optional trailing text:\n"
	        "  kind conv who ctype flag noflag mtime timeout n cat, text=|texteq= last\n");
}

static void do_command(char *line)
{
	char *cmd = g_strstrip(line);
	char *arg = split_word(cmd);
	gsize i;

	if (!*cmd || *cmd == '#') {
		g_idle_add(next_command, NULL);
		return;
	}
	if (pending_cmd)
		g_free(pending_cmd), pending_cmd = NULL;

	for (i = 0; i < G_N_ELEMENTS(commands); i++) {
		if (!strcmp(cmd, commands[i].name)) {
			commands[i].handler(cmd, arg);
			return;
		}
	}
	fail_now(cmd, "unknown command");
}

gboolean next_command(gpointer unused)
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
