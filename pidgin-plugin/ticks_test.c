/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file ticks_test.c Test hooks of pidgin-xmpp2-ticks (XMPP2_TEST_HOOKS)
 *
 * For tests/integration/test_ticks.sh: with XMPP2_TICKS_TEST_PEER set,
 * prpl-xmpp2 accounts are connected (Pidgin runs with -n), the first one
 * that signs on opens an IM with that peer and sends one line through the
 * normal UI path; every update logs the line as the view shows it; after
 * the "displayed" one the hook clears the scrollback and closes the
 * conversation.  Never built into release packages.
 */
#include <glib.h>

#include "account.h"
#include "connection.h"
#include "conversation.h"
#include "debug.h"
#include "eventloop.h"
#include "plugin.h"
#include "signals.h"
#include "util.h"

#include "tickbuf.h"
#include "ticks.h"

/* Outside the prpl's signal emission: clear the view, then close it. */
static gboolean
test_close_cb(gpointer data)
{
	PurpleAccount *account = purple_accounts_find(data, PRPL_ID);
	const char *peer = g_getenv("XMPP2_TICKS_TEST_PEER");
	PurpleConversation *conv;
	X2TickBuf *tb;

	g_free(data);
	conv = account ? purple_find_conversation_with_account(
		PURPLE_CONV_TYPE_IM, peer, account) : NULL;
	if (conv == NULL) {
		purple_debug_error(LOG, "test: conversation with %s is gone\n",
		                   peer);
		return FALSE;
	}
	purple_conversation_clear_message_history(conv);
	tb = x2_ticks_view(conv, FALSE);
	purple_debug_info(LOG, "test: after clear %u lines tracked\n",
	                  tb ? x2_tick_buf_size(tb) : 0);
	purple_conversation_destroy(conv);
	purple_debug_info(LOG, "test: conversation closed\n");
	return FALSE;
}

void
x2_ticks_test_rendered(PurpleConversation *conv, X2TickBuf *tb, const char *id)
{
	char *line = x2_tick_buf_line_text(tb, id);
	PurpleAccount *account = purple_conversation_get_account(conv);

	purple_debug_info(LOG, "test: line %s reads [%s]\n", id,
	                  line ? line : "(none)");
	g_free(line);
	if (g_getenv("XMPP2_TICKS_TEST_PEER") != NULL &&
	    x2_tick_buf_state(tb, id) == X2_TICK_DISPLAYED)
		g_idle_add(test_close_cb,
		           g_strdup(purple_account_get_username(account)));
}

static gboolean
test_send_cb(gpointer data)
{
	PurpleAccount *account = data;
	const char *peer = g_getenv("XMPP2_TICKS_TEST_PEER");
	PurpleConversation *conv;

	conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, peer);
	purple_conv_im_send(PURPLE_CONV_IM(conv), "ticks-test-1");
	purple_debug_info(LOG, "test: sent to %s\n", peer);
	return FALSE;
}

static void
test_signed_on_cb(PurpleConnection *gc, gpointer data)
{
	static gboolean sent;
	PurpleAccount *account = purple_connection_get_account(gc);

	if (sent || g_getenv("XMPP2_TICKS_TEST_PEER") == NULL ||
	    !purple_strequal(purple_account_get_protocol_id(account), PRPL_ID))
		return;
	sent = TRUE;
	/* after the presence exchange, so the peer's caps are known */
	purple_timeout_add_seconds(3, test_send_cb, account);
}

/* The smoke runs start Pidgin with -n (no login): sign in ourselves. */
static gboolean
test_connect_cb(gpointer data)
{
	GList *l;

	for (l = purple_accounts_get_all(); l != NULL; l = l->next) {
		PurpleAccount *account = l->data;

		if (purple_strequal(purple_account_get_protocol_id(account),
		                    PRPL_ID) &&
		    purple_account_is_disconnected(account)) {
			purple_debug_info(LOG, "test: connecting %s\n",
			                  purple_account_get_username(account));
			purple_account_connect(account);
		}
	}
	return FALSE;
}

void
x2_ticks_test_load(PurplePlugin *plugin)
{
	purple_signal_connect(purple_connections_get_handle(), "signed-on",
	                      plugin, PURPLE_CALLBACK(test_signed_on_cb), NULL);
	if (g_getenv("XMPP2_TICKS_TEST_PEER") != NULL)
		purple_timeout_add_seconds(1, test_connect_cb, NULL);
}
