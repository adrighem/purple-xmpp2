/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */

/* libpurple UI ops: debug capture, conversation/notify EV lines, request recorder. */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"

GPtrArray *evs;
GPtrArray *dbgs;
guint ev_cursor, dbg_cursor;
gboolean debug_echo;

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
	/* every account of the userdir: migration tests run clones next to --user */
	if (!message)
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

/* purple_notify_message(): an EV of kind "notify" (no conversation) */
static void *null_notify_message(PurpleNotifyMsgType type, const char *title,
                                 const char *primary, const char *secondary)
{
	Ev *e = g_new0(Ev, 1);

	(void)type;
	(void)title;
	e->kind = "notify";
	e->conv = g_strdup("-");
	e->who = g_strdup("-");
	e->text = g_strdup_printf("%s%s%s", primary ? primary : "",
	                          secondary ? " " : "", secondary ? secondary : "");
	e->mtime = time(NULL);
	g_ptr_array_add(evs, e);
	fprintf(out, "EV notify conv=- who=- flags=0x0 mtime=%ld names= ctype=im %s\n",
	        (long)e->mtime, e->text);
	fflush(out);
	poll_pending();
	return NULL;
}

static PurpleNotifyUiOps notify_ops = { null_notify_message, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };

static PurpleConversationUiOps conv_ops = {
	NULL, NULL, NULL, NULL, null_write_conv, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

/* purple_request_action() has no real UI here: the request is recorded and
 * a script answers it with "answer N". */
static GCallback req_cbs[8];
static guint req_n;
static void *req_data;
static char *req_text;

static void req_clear(void)
{
	req_n = 0;
	g_free(req_text);
	req_text = NULL;
}

static void *request_action_ui(const char *title, const char *primary,
                               const char *secondary, int default_action,
                               PurpleAccount *acct, const char *who,
                               PurpleConversation *conv, void *user_data,
                               size_t action_count, va_list actions)
{
	GString *b = g_string_new(NULL);
	size_t i;

	req_clear();
	for (i = 0; i < action_count; i++) {
		const char *label = va_arg(actions, const char *);
		GCallback cb = va_arg(actions, GCallback);

		if (i < G_N_ELEMENTS(req_cbs))
			req_cbs[req_n++] = cb;
		g_string_append_printf(b, "%s%s", i ? "|" : "", label);
	}
	req_data = user_data;
	req_text = g_strdup_printf("%s [%s]", primary, b->str);
	g_string_free(b, TRUE);
	fprintf(out, "REQUEST %s\n", req_text);
	fflush(out);
	return &req_n;
}

static void request_close_ui(PurpleRequestType type, void *ui_handle)
{
	req_clear();
}

/* libpurple writes blist.xml only when the buddy list has a UI */
static PurpleBlistUiOps blist_ops;

static PurpleRequestUiOps request_ops = {
	.request_action = request_action_ui,
	.close_request = request_close_ui,
};
static guint answer_idx;

static gboolean poll_answer(char **detail)
{
	GCallback cb;
	void *data;

	if (req_n == 0)
		return FALSE;
	if (answer_idx >= req_n) {
		*detail = g_strdup_printf("no button %u in %s", answer_idx, req_text);
		return TRUE;
	}
	cb = req_cbs[answer_idx];
	data = req_data;
	*detail = g_strdup_printf("%s -> b%u", req_text, answer_idx);
	req_clear();
	if (cb)
		((PurpleRequestActionCb)cb)(data, (int)answer_idx);
	return TRUE;
}

static gboolean poll_request(char **detail)
{
	return req_n != 0;
}


void cmd_answer(const char *cmd, char *arg)
{
	char *sp = strchr(arg, ' ');
	double secs = sp ? atof(sp + 1) : 10;

	answer_idx = (guint)atoi(arg);
	poll_arm(P_POLL, cmd, secs, poll_answer);
}

void cmd_no_request(const char *cmd, char *arg)
{
	poll_arm(P_POLL_ABSENT, cmd, *arg ? atof(arg) : 3, poll_request);
}

/* Install the UI ops; call before purple_core_init(). */
void harness_ui_init(void)
{
	evs = g_ptr_array_new();
	dbgs = g_ptr_array_new();
	purple_debug_set_ui_ops(&debug_ops);
	purple_debug_set_enabled(TRUE);
	purple_conversations_set_ui_ops(&conv_ops);
	purple_request_set_ui_ops(&request_ops);
	purple_notify_set_ui_ops(&notify_ops);
}

/* libpurple writes blist.xml only when the buddy list has a UI. */
void harness_ui_blist(void)
{
	purple_blist_set_ui_ops(&blist_ops);
}
