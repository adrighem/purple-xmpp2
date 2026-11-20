/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */

/* Message commands: send, cmd/correct, focus, raw XML. */
#include <string.h>

#include "harness.h"

void cmd_send(const char *cmd, char *arg)
{
	char *text = split_word(arg);
	PurpleConversation *conv;

	if (!*text || !need_online("send"))
		return;
	conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, arg);
	purple_conv_im_send(purple_conversation_get_im_data(conv), text);
	continue_ok("send", arg);
}

/* "cmd TARGET TEXT" and its "correct JID TEXT" shorthand. */
void cmd_cmd(const char *cmd, char *arg)
{
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
}

void cmd_focus(const char *cmd, char *arg)
{
	PurpleConversation *c = find_conv(arg);

	if (!c) {
		fail_now("focus", "no such conversation");
		return;
	}
	purple_conversation_set_data(c, "unseen-count", GINT_TO_POINTER(0));
	purple_conversation_update(c, PURPLE_CONV_UPDATE_UNSEEN);
	continue_ok("focus", arg);
}

/* "raw XML" must be accepted by send_raw, "raw-refused XML" must be refused. */
void cmd_raw(const char *cmd, char *arg)
{
	PurpleConnection *gc = purple_account_get_connection(account);
	PurplePluginProtocolInfo *info;
	gboolean want_refused = !strcmp(cmd, "raw-refused");
	gboolean refused;

	if (!*arg || !need_online(cmd))
		return;
	info = PURPLE_PLUGIN_PROTOCOL_INFO(purple_connection_get_prpl(gc));
	if (info->send_raw == NULL) {
		fail_now(cmd, "the prpl has no send_raw");
		return;
	}
	refused = info->send_raw(gc, arg, strlen(arg)) < 0;
	if (refused != want_refused) {
		fail_now(cmd, refused ? "refused" : "accepted");
		return;
	}
	continue_ok(cmd, arg);
}
