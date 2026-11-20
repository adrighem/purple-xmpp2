/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file ticks.c Pidgin plugin: delivery and read ticks for prpl-xmpp2
 *
 * Correlation (ARCHITECTURE 7.9): libpurple sends an IM and writes the sent
 * line in one call stack (common_send(): serv_send_im(), then
 * purple_conv_im_write()).  prpl-xmpp2 emits "xmpp2-sent-message" while
 * sending; we keep that id as pending and attach it to the next
 * PURPLE_MESSAGE_SEND line Pidgin displays in that conversation
 * ("displayed-im-msg").  The pending id is dropped at "sent-im-msg" (the end
 * of common_send()) and by a high priority idle, so it never reaches a line
 * written later, such as a carbon of our other device.  "xmpp2-receipt" and
 * "xmpp2-marker" then update the line through tickbuf.c.
 *
 * The prpl may be missing or load later: its signals are connected when it
 * is (or becomes) loaded and forgotten when it unloads.
 */
#include <glib.h>

#ifndef PURPLE_PLUGINS
#define PURPLE_PLUGINS
#endif

#include "account.h"
#include "conversation.h"
#include "debug.h"
#include "plugin.h"
#include "pluginpref.h"
#include "prefs.h"
#include "signals.h"
#include "util.h"
#include "version.h"

#include "gtkconv.h"
#include "gtkplugin.h"

#include "tickbuf.h"
#include "tickmap.h"
#include "ticks.h"

#define PLUGIN_ID "gtk-xmpp2-ticks"

#define PREF_ROOT "/plugins/gtk/xmpp2-ticks"
#define PREF_ENABLED PREF_ROOT "/enabled"
#define PREF_STYLE PREF_ROOT "/style"

/* Sent lines remembered per conversation view; the oldest is dropped */
#define LINES_PER_VIEW 200

/* GObject data key of the X2TickBuf on a conversation's GtkIMHtml */
#define VIEW_KEY "xmpp2-ticks"

static PurplePlugin *ticks_plugin;
static PurplePlugin *prpl;      /* prpl-xmpp2 while its signals are ours */

/* The id the prpl just sent, waiting for its line */
static struct {
	PurpleAccount *account;
	char *who;
	char *id;
	guint idle;
} pending;

static void
pending_clear(void)
{
	g_free(pending.who);
	g_free(pending.id);
	pending.who = NULL;
	pending.id = NULL;
	pending.account = NULL;
	if (pending.idle != 0)
		g_source_remove(pending.idle);
	pending.idle = 0;
}

static gboolean
pending_expire_cb(gpointer data)
{
	pending.idle = 0;
	if (pending.id != NULL)
		purple_debug_info(LOG, "no line was written for %s\n", pending.id);
	pending_clear();
	return FALSE;
}

static gboolean
is_xmpp2_im(PurpleConversation *conv)
{
	PurpleAccount *account;

	if (conv == NULL ||
	    purple_conversation_get_type(conv) != PURPLE_CONV_TYPE_IM)
		return FALSE;
	account = purple_conversation_get_account(conv);
	return account != NULL &&
	       purple_strequal(purple_account_get_protocol_id(account), PRPL_ID);
}

static void
view_destroy_cb(GtkWidget *view, gpointer data)
{
	X2TickBuf *tb = g_object_get_data(G_OBJECT(view), VIEW_KEY);

	g_object_set_data(G_OBJECT(view), VIEW_KEY, NULL);
	x2_tick_buf_free(tb);
}

/*
 * The tick state lives on the conversation's GtkIMHtml and dies with it
 * (window closed, conversation destroyed, view rebuilt): no stale pointers
 * to keep track of.  Contact-merged IMs share one view, and the ids are
 * unique anyway.
 */
X2TickBuf *
x2_ticks_view(PurpleConversation *conv, gboolean create)
{
	PidginConversation *gtkconv;
	GtkWidget *view;
	X2TickBuf *tb;

	if (!is_xmpp2_im(conv) || !PIDGIN_IS_PIDGIN_CONVERSATION(conv))
		return NULL;
	gtkconv = PIDGIN_CONVERSATION(conv);
	view = gtkconv ? gtkconv->imhtml : NULL;
	if (view == NULL)
		return NULL;

	tb = g_object_get_data(G_OBJECT(view), VIEW_KEY);
	if (tb != NULL || !create)
		return tb;

	tb = x2_tick_buf_new(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)),
	                     LINES_PER_VIEW);
	g_object_set_data(G_OBJECT(view), VIEW_KEY, tb);
	g_signal_connect(view, "destroy", G_CALLBACK(view_destroy_cb), NULL);
	return tb;
}

static void
drop_tick_buf(PurpleConversation *conv)
{
	PidginConversation *gtkconv;
	X2TickBuf *tb = x2_ticks_view(conv, FALSE);

	if (tb == NULL)
		return;
	gtkconv = PIDGIN_CONVERSATION(conv);
	g_signal_handlers_disconnect_by_func(gtkconv->imhtml,
	                                     G_CALLBACK(view_destroy_cb), NULL);
	view_destroy_cb(gtkconv->imhtml, NULL);
}

static gboolean
same_peer(PurpleAccount *account, const char *a, const char *b)
{
	char *na = g_strdup(purple_normalize(account, a));
	gboolean same = purple_strequal(na, purple_normalize(account, b));

	g_free(na);
	return same;
}

/* prpl: an IM is going out; its line is written next */
static void
sent_message_cb(PurpleAccount *account, const char *who, const char *id,
                const char *body, gpointer data)
{
	pending_clear();
	if (!purple_prefs_get_bool(PREF_ENABLED) || id == NULL || who == NULL)
		return;
	pending.account = account;
	pending.who = g_strdup(who);
	pending.id = g_strdup(id);
	pending.idle = g_idle_add_full(G_PRIORITY_HIGH, pending_expire_cb,
	                               NULL, NULL);
}

/* Pidgin: a line was appended to the view */
static void
displayed_im_cb(PurpleAccount *account, const char *who, const char *message,
                PurpleConversation *conv, PurpleMessageFlags flags,
                gpointer data)
{
	X2TickBuf *tb;

	if (pending.id == NULL || !(flags & PURPLE_MESSAGE_SEND) ||
	    account != pending.account ||
	    !same_peer(account, purple_conversation_get_name(conv), pending.who))
		return;
	tb = x2_ticks_view(conv, TRUE);
	if (tb != NULL) {
		x2_tick_buf_record(tb, pending.id);
		purple_debug_info(LOG, "line for %s recorded (%u in view)\n",
		                  pending.id, x2_tick_buf_size(tb));
	}
	pending_clear();
}

/* libpurple: end of common_send(), whether a line was written or not */
static void
sent_im_cb(PurpleAccount *account, const char *who, const char *message,
           gpointer data)
{
	pending_clear();
}

static void
apply_state(PurpleAccount *account, const char *who, const char *id,
            X2TickState state)
{
	PurpleConversation *conv;
	X2TickBuf *tb;
	X2TickStyle style;

	if (!purple_prefs_get_bool(PREF_ENABLED) || state == X2_TICK_NONE ||
	    who == NULL || id == NULL)
		return;
	conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, who,
	                                             account);
	tb = x2_ticks_view(conv, FALSE);
	if (tb == NULL)
		return;
	style = x2_tick_style_from_string(purple_prefs_get_string(PREF_STYLE));
	if (!x2_tick_buf_update(tb, id, state, style)) {
		purple_debug_info(LOG, "no line for %s from %s\n", id, who);
		return;
	}
	purple_debug_info(LOG, "%s from %s now %s\n", id, who,
	                  state == X2_TICK_DISPLAYED ? "displayed" : "delivered");
#ifdef XMPP2_TEST_HOOKS
	x2_ticks_test_rendered(conv, tb, id);
#endif
}

static void
receipt_cb(PurpleAccount *account, const char *who, const char *id,
           gpointer data)
{
	apply_state(account, who, id, X2_TICK_DELIVERED);
}

static void
marker_cb(PurpleAccount *account, const char *who, const char *id,
          const char *marker, gpointer data)
{
	apply_state(account, who, id, x2_tick_state_from_marker(marker));
}

/* "Clear Scrollback" empties the view: forget its lines at once */
static void
cleared_history_cb(PurpleConversation *conv, gpointer data)
{
	X2TickBuf *tb = x2_ticks_view(conv, FALSE);

	if (tb != NULL)
		x2_tick_buf_clear(tb);
}

static void
connect_prpl(PurplePlugin *p)
{
	if (prpl != NULL || p == NULL || !purple_plugin_is_loaded(p))
		return;
	/* the newest signal first: an older prpl without it gets no handlers */
	if (purple_signal_connect(p, "xmpp2-sent-message", ticks_plugin,
	                          PURPLE_CALLBACK(sent_message_cb), NULL) == 0) {
		purple_debug_error(LOG, "%s has no xmpp2-sent-message signal; "
		                   "install matching purple-xmpp2 and "
		                   "pidgin-xmpp2-ticks versions\n", PRPL_ID);
		return;
	}
	purple_signal_connect(p, "xmpp2-receipt", ticks_plugin,
	                      PURPLE_CALLBACK(receipt_cb), NULL);
	purple_signal_connect(p, "xmpp2-marker", ticks_plugin,
	                      PURPLE_CALLBACK(marker_cb), NULL);
	prpl = p;
	purple_debug_info(LOG, "connected to %s signals\n", PRPL_ID);
}

static void
plugin_load_cb(PurplePlugin *p, gpointer data)
{
	if (p->info != NULL && purple_strequal(p->info->id, PRPL_ID))
		connect_prpl(p);
}

/* The prpl's signals and our handlers on them go with its instance */
static void
plugin_unload_cb(PurplePlugin *p, gpointer data)
{
	if (p != prpl)
		return;
	pending_clear();
	prpl = NULL;
	purple_debug_info(LOG, "%s unloaded\n", PRPL_ID);
}

static gboolean
plugin_load(PurplePlugin *plugin)
{
	PurplePlugin *p;

	ticks_plugin = plugin;
	purple_signal_connect(purple_plugins_get_handle(), "plugin-load", plugin,
	                      PURPLE_CALLBACK(plugin_load_cb), NULL);
	purple_signal_connect(purple_plugins_get_handle(), "plugin-unload",
	                      plugin, PURPLE_CALLBACK(plugin_unload_cb), NULL);
	purple_signal_connect(pidgin_conversations_get_handle(),
	                      "displayed-im-msg", plugin,
	                      PURPLE_CALLBACK(displayed_im_cb), NULL);
	purple_signal_connect(purple_conversations_get_handle(), "sent-im-msg",
	                      plugin, PURPLE_CALLBACK(sent_im_cb), NULL);
	purple_signal_connect(purple_conversations_get_handle(),
	                      "cleared-message-history", plugin,
	                      PURPLE_CALLBACK(cleared_history_cb), NULL);
#ifdef XMPP2_TEST_HOOKS
	x2_ticks_test_load(plugin);
#endif

	p = purple_plugins_find_with_id(PRPL_ID);
	if (p != NULL)
		connect_prpl(p);
	if (prpl == NULL)
		purple_debug_info(LOG, "%s not loaded yet; waiting for it\n",
		                  PRPL_ID);
	purple_debug_info(LOG, "loaded\n");
	return TRUE;
}

static gboolean
plugin_unload(PurplePlugin *plugin)
{
	GList *l;

	purple_signals_disconnect_by_handle(plugin);
	pending_clear();
	prpl = NULL;
	/* No callback of ours may outlive the module; the glyphs stay. */
	for (l = purple_get_ims(); l != NULL; l = l->next)
		drop_tick_buf(l->data);
	ticks_plugin = NULL;
	return TRUE;
}

static PurplePluginPrefFrame *
get_pref_frame(PurplePlugin *plugin)
{
	PurplePluginPrefFrame *frame = purple_plugin_pref_frame_new();
	PurplePluginPref *pref;

	pref = purple_plugin_pref_new_with_name_and_label(PREF_ENABLED,
		"Show delivery and read ticks on sent messages");
	purple_plugin_pref_frame_add(frame, pref);

	pref = purple_plugin_pref_new_with_name_and_label(PREF_STYLE,
		"Tick style");
	purple_plugin_pref_set_type(pref, PURPLE_PLUGIN_PREF_CHOICE);
	purple_plugin_pref_add_choice(pref,
		"Check marks: \xe2\x9c\x93 delivered, \xe2\x9c\x93\xe2\x9c\x93 read",
		"checks");
	purple_plugin_pref_add_choice(pref, "ASCII: + delivered, ++ read",
	                              "ascii");
	purple_plugin_pref_add_choice(pref, "Words: (delivered), (read)",
	                              "words");
	purple_plugin_pref_frame_add(frame, pref);
	return frame;
}

static PurplePluginUiInfo prefs_info = {
	get_pref_frame,
	0,
	NULL,
	NULL, NULL, NULL, NULL
};

static PurplePluginInfo info = {
	PURPLE_PLUGIN_MAGIC,
	PURPLE_MAJOR_VERSION,
	PURPLE_MINOR_VERSION,
	PURPLE_PLUGIN_STANDARD,
	PIDGIN_PLUGIN_TYPE,
	0,
	NULL,
	PURPLE_PRIORITY_DEFAULT,
	PLUGIN_ID,
	"XMPP2 Delivery Ticks",
	XMPP2_VERSION,
	"Delivery and read ticks for purple-xmpp2 conversations",
	"Appends a tick to your sent messages in XMPP (purple-xmpp2) "
	"conversations: one when the contact's client confirms delivery "
	"(XEP-0184 receipt or XEP-0333 received marker), two when it reports "
	"the message as displayed (XEP-0333).  Needs the prpl-xmpp2 protocol "
	"plugin; does nothing for other accounts.",
	"purple-xmpp2 contributors",
	"https://github.com/adrighem/purple-xmpp2",
	plugin_load,
	plugin_unload,
	NULL,
	NULL,
	NULL,
	&prefs_info,
	NULL,
	NULL, NULL, NULL, NULL
};

static void
init_plugin(PurplePlugin *plugin)
{
	purple_prefs_add_none(PREF_ROOT);
	purple_prefs_add_bool(PREF_ENABLED, TRUE);
	purple_prefs_add_string(PREF_STYLE, "checks");
}

PURPLE_INIT_PLUGIN(xmpp2_ticks, init_plugin, info)
