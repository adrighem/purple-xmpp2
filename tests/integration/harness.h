/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 *
 * xmpp2-harness shared declarations. See harness_script.c for the command
 * table and the script language reference.
 */
#ifndef XMPP2_HARNESS_H
#define XMPP2_HARNESS_H

#include <glib.h>
#include <stdio.h>
#include <time.h>

#include "purple.h"

#define UI_ID "xmpp2harness"

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

typedef struct {
	char *kind, *conv, *who, *ctype, *cat, *text, *texteq;
	guint flag_req, flag_no;
	time_t mt_epoch;
	int mt_secs;
	double timeout;
	int n;
} Filter;

/* What the main loop is waiting for between two script commands. */
typedef enum {
	P_NONE, P_CONNECT, P_DISCONNECT, P_SLEEP, P_JOIN, P_XFER,
	P_EXPECT_EV, P_ABSENT_EV, P_EXPECT_DBG, P_ABSENT_DBG,
	P_POLL, P_POLL_ABSENT
} Pending;

/* A script command: handler(name as typed, rest of the line). */
typedef void (*CommandFn)(const char *cmd, char *arg);

typedef struct {
	const char *name;
	CommandFn handler;
	const char *help;     /* "ARGS  description", shown by --help */
} Command;

/* ---- shared state (defined in harness_core.c and harness_ui.c) ---- */
extern GPtrArray *evs;    /* of Ev* */
extern GPtrArray *dbgs;   /* of Dbg* */
extern guint ev_cursor, dbg_cursor;
extern gboolean debug_echo;
extern FILE *out;         /* real stdout; libpurple prints stray lines to fd 1 */
extern GMainLoop *loop;
extern PurpleAccount *account;
extern GQueue *script;    /* of char* */
extern Pending pending;
extern char *pending_cmd;
extern char *pending_text;    /* join: room */
extern Filter *pending_filter;
extern gboolean pending_next, pending_fail_ok;
extern int exit_code;
extern const char *userdir_path;
extern int c2s_port;

/* ---- harness_core.c: run state, completion and polling ---- */
void finish(int code);
void say(gboolean ok, const char *cmd, const char *detail);
void resume(gboolean ok, const char *detail);
void fail_now(const char *cmd, const char *detail);
void continue_ok(const char *cmd, const char *detail);
void poll_pending(void);
void arm(Pending p, const char *cmd, double secs);
void poll_arm(Pending p, const char *cmd, double secs,
              gboolean (*fn)(char **detail));
gboolean need_online(const char *cmd);
char *split_word(char *s);
PurpleConversation *find_conv(const char *name);
void harness_connect_signals(void);

/* ---- harness_filter.c: flag names and EV/debug filters ---- */
char *flag_list(guint flags);
void filter_free(Filter *f);
Filter *filter_parse(const char *args, double default_timeout, char **err);
int ev_find(const Filter *f, guint from);
int dbg_find(const Filter *f, guint from);
int ev_count(const Filter *f);
int dbg_count(const Filter *f);

/* ---- harness_ui.c: libpurple UI ops (conversation, notify, request, debug) ---- */
void harness_ui_init(void);       /* before purple_core_init() */
void harness_ui_blist(void);      /* after purple_blist_new() */
void cmd_answer(const char *cmd, char *arg);
void cmd_no_request(const char *cmd, char *arg);

/* ---- harness_script.c: parser, dispatch table, --help ---- */
gboolean next_command(gpointer unused);
void harness_print_help(FILE *fp);

/*
 * Command handlers. To add a command: write the handler in the matching
 * harness_cmds_*.c, declare it here and add one line to commands[] in
 * harness_script.c.
 */
/* harness_cmds_session.c */
int drop_c2s_sockets(void);
void set_pref(PurpleAccount *acct, const char *spec);
void cmd_connect(const char *cmd, char *arg);
void cmd_disconnect(const char *cmd, char *arg);
void cmd_drop(const char *cmd, char *arg);
void cmd_sleep(const char *cmd, char *arg);
void cmd_quit(const char *cmd, char *arg);
void cmd_remove_account(const char *cmd, char *arg);
void cmd_pref(const char *cmd, char *arg);
void cmd_buddy_add(const char *cmd, char *arg);
void cmd_expect_account(const char *cmd, char *arg);
void cmd_action(const char *cmd, char *arg);
void cmd_plugin_action(const char *cmd, char *arg);
/* harness_cmds_msg.c */
void cmd_send(const char *cmd, char *arg);
void cmd_cmd(const char *cmd, char *arg);       /* "cmd" and "correct" */
void cmd_focus(const char *cmd, char *arg);
void cmd_raw(const char *cmd, char *arg);       /* "raw" and "raw-refused" */
/* harness_cmds_rooms.c */
void cmd_join(const char *cmd, char *arg);
void cmd_leave(const char *cmd, char *arg);
void cmd_groupsend(const char *cmd, char *arg);
/* harness_cmds_files.c */
void cmd_upload(const char *cmd, char *arg);    /* upload, upload-fail, upload-drop */
/* harness_cmds_xfer.c: transfer progress, mid-transfer cancel, RSS, --proxy */
void harness_xfer_init(const char *proxy);      /* after the account exists */
void cmd_xfer_cancel_at(const char *cmd, char *arg);
void cmd_rss_reset(const char *cmd, char *arg);
void cmd_rss_check(const char *cmd, char *arg);
/* harness_cmds_assert.c */
void cmd_assert(const char *cmd, char *arg);    /* expect-*, no-*, count-* */
void cmd_legacy(const char *cmd, char *arg);    /* wait, absent, count */
void cmd_clear_ev(const char *cmd, char *arg);
void cmd_clear_debug(const char *cmd, char *arg);
void cmd_dump_debug(const char *cmd, char *arg);
void cmd_logcount(const char *cmd, char *arg);

#endif
