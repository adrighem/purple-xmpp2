/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */

/* Room commands: join, leave, groupsend. */
#include "harness.h"

void cmd_join(const char *cmd, char *arg)
{
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
}

void cmd_leave(const char *cmd, char *arg)
{
	PurpleConversation *c = find_conv(arg);

	if (!c) {
		fail_now("leave", "no such conversation");
		return;
	}
	purple_conversation_destroy(c);
	continue_ok("leave", arg);
}

void cmd_groupsend(const char *cmd, char *arg)
{
	char *text = split_word(arg);
	PurpleConversation *c = find_conv(arg);

	if (!c || purple_conversation_get_type(c) != PURPLE_CONV_TYPE_CHAT || !*text) {
		fail_now("groupsend", "no such room or empty text");
		return;
	}
	purple_conv_chat_send(purple_conversation_get_chat_data(c), text);
	continue_ok("groupsend", arg);
}
