/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file correct_cmd.c The /correct command (XEP-0308, outgoing)
 *
 * "/correct TEXT" sends TEXT as a new message that replaces the last message
 * we sent in the conversation.  The text goes through the normal send path
 * (so it is shown and logged like any sent line); the outbound hook in
 * correct.c adds <replace/> because state->pending is set meanwhile.
 */
#include "xmpp2_internal.h"

#include "cmds.h"
#include "conversation.h"
#include "debug.h"
#include "util.h"

#include "ext/correct.h"
#include "ext/msgctx.h"

static PurpleCmdId cmd_id;

static PurpleCmdRet
fail(char **error, const char *msg)
{
	*error = g_strdup(msg);
	return PURPLE_CMD_RET_FAILED;
}

static PurpleCmdRet
cmd_correct(PurpleConversation *conv, const gchar *cmd, gchar **args,
            gchar **error, void *data)
{
	PurpleConnection *gc = purple_conversation_get_gc(conv);
	JabberStream *js = gc ? purple_connection_get_protocol_data(gc) : NULL;
	X2CorrectState *st = x2_correct_state(js);
	gboolean chat = purple_conversation_get_type(conv) == PURPLE_CONV_TYPE_CHAT;
	const char *last;
	char *peer, *html;

	if (st == NULL)
		return fail(error, _("Not connected."));
	if (args[0] == NULL || *args[0] == '\0')
		return fail(error, _("Usage: /correct TEXT"));

	peer = x2_msg_peer_of(js, purple_conversation_get_name(conv),
	                      chat ? "groupchat" : "chat", NULL);
	last = peer ? x2_corrlog_last_sent(st->log, peer) : NULL;
	g_free(peer);
	if (last == NULL)
		return fail(error, _("There is no earlier message of yours in "
		                     "this conversation to correct."));

	html = purple_markup_escape_text(args[0], -1);
	st->pending = g_strdup(last);
	if (chat)
		purple_conv_chat_send(PURPLE_CONV_CHAT(conv), html);
	else
		purple_conv_im_send(PURPLE_CONV_IM(conv), html);
	g_free(st->pending);
	st->pending = NULL;
	g_free(html);
	return PURPLE_CMD_RET_OK;
}

void
x2_correct_cmd_register(void)
{
	cmd_id = purple_cmd_register("correct", "s", PURPLE_CMD_P_PRPL,
	                             PURPLE_CMD_FLAG_IM | PURPLE_CMD_FLAG_CHAT |
	                             PURPLE_CMD_FLAG_PRPL_ONLY |
	                             PURPLE_CMD_FLAG_ALLOW_WRONG_ARGS,
	                             XMPP2_PRPL_ID, cmd_correct,
	                             _("correct TEXT:  Replace your last "
	                               "message in this conversation with "
	                               "TEXT (XEP-0308)"), NULL);
}

void
x2_correct_cmd_unregister(void)
{
	if (cmd_id != 0)
		purple_cmd_unregister(cmd_id);
	cmd_id = 0;
}
