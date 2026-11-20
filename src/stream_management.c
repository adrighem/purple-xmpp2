/*
 * purple - Jabber Protocol Plugin
 *
 * Purple is the legal property of its developers, whose names are too numerous
 * to list here.  Please refer to the COPYRIGHT file distributed with this
 * source distribution.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA
 *
 */

/*
 * XEP-0198 stream management (M9, M10; see docs/ARCHITECTURE.md 7.8).
 *
 * State lives per PurpleAccount in sm_sessions (not in the JabberStream).
 * The counters and the bounded unacked queue are core/smstate.c; the
 * silent reconnect for <resume/> is sm_resume.c.
 *
 * - <enable resume='true'/> unless the account option "sm_resume" is off.
 *   Every <enable/> starts from zero: the queue of an earlier session is
 *   never resent on a fresh session, only after <resumed/>.
 * - Every outbound stanza is copied into the queue (a resume resends all
 *   of them); only <message> stanzas with a body are ever reported.
 * - <r/> is batched: after SM_REQUEST_EVERY stanzas, or SM_REQUEST_IDLE_MS
 *   after the last stanza.  A server <r/> is answered at once.  Neither
 *   goes out while a resumption is reconnecting.
 * - On a connection error or sign-off, unacked <message> stanzas with a
 *   body are shown in their conversation as "may not have been delivered".
 * - Raw XML (XML console) is parsed and counted, see
 *   jabber_sm_outbound_raw().
 */

#include "xmpp2_internal.h"

#include <glib.h>
#include <string.h>
#include "namespaces.h"
#include "xmlnode.h"
#include "jabber.h"
#include "debug.h"
#include "notify.h"
#include "conversation.h"
#include "signals.h"
#include "stream_management.h"
#include "sm_resume.h"
#include "smstate.h"

#define SM_DBG "xmpp2-sm"
#define SM_MAX_QUEUE 1000
#define SM_REQUEST_EVERY 5
#define SM_REQUEST_IDLE_MS 2000

typedef struct {
	X2SmState *st;
	guint request_timer;
	/* From <enabled/>, for <resume previd/> (sm_resume.c) */
	char *resume_id;
	char *resume_location;
	guint resume_max;
	gboolean resumable;
} SmSession;

/* PurpleAccount * -> SmSession * */
static GHashTable *sm_sessions;
static int sm_signal_handle;
static gboolean sm_signal_connected;

static void
sm_session_free(gpointer p)
{
	SmSession *s = p;

	if (s->request_timer)
		purple_timeout_remove(s->request_timer);
	x2_sm_state_free(s->st);
	g_free(s->resume_id);
	g_free(s->resume_location);
	g_free(s);
}

static SmSession *
sm_session_lookup(JabberStream *js)
{
	return g_hash_table_lookup(sm_sessions,
	                           purple_connection_get_account(js->gc));
}

static SmSession *
sm_session_get(JabberStream *js)
{
	PurpleAccount *account = purple_connection_get_account(js->gc);
	SmSession *s = g_hash_table_lookup(sm_sessions, account);

	if (s == NULL) {
		s = g_new0(SmSession, 1);
		s->st = x2_sm_state_new(SM_MAX_QUEUE,
		                        (GDestroyNotify)xmlnode_free);
		g_hash_table_insert(sm_sessions, account, s);
	}
	return s;
}

static void
sm_send_request(JabberStream *js, SmSession *s)
{
	xmlnode *req;

	if (s->request_timer) {
		purple_timeout_remove(s->request_timer);
		s->request_timer = 0;
	}
	/* Reconnecting: the request goes out after <resumed/>. */
	if (js->sm_resume != NULL)
		return;
	x2_sm_state_requested(s->st);
	purple_debug_info(SM_DBG, "request ack: sent=%u acked=%u\n",
	                  x2_sm_state_outbound(s->st),
	                  x2_sm_state_acked(s->st));
	req = xmlnode_new("r");
	xmlnode_set_namespace(req, NS_STREAM_MANAGEMENT);
	jabber_send(js, req);
	xmlnode_free(req);
}

static gboolean
sm_request_idle_cb(gpointer data)
{
	PurpleAccount *account = data;
	PurpleConnection *gc = purple_account_get_connection(account);
	JabberStream *js = gc ? purple_connection_get_protocol_data(gc) : NULL;
	SmSession *s = g_hash_table_lookup(sm_sessions, account);

	if (s == NULL)
		return FALSE;
	s->request_timer = 0;
	if (js != NULL && js->sm_state == SM_ENABLED && js->sm_resume == NULL
	    && x2_sm_state_need_request(s->st, 1))
		sm_send_request(js, s);
	return FALSE;
}

static void
sm_arm_idle_request(JabberStream *js, SmSession *s)
{
	if (s->request_timer)
		purple_timeout_remove(s->request_timer);
	s->request_timer = purple_timeout_add(SM_REQUEST_IDLE_MS,
	                                      sm_request_idle_cb,
	                                      purple_connection_get_account(js->gc));
}

/* A <message> with a body and a recipient: what the user may be told about */
static gboolean
sm_is_reportable(xmlnode *stanza)
{
	return purple_strequal(stanza->name, "message")
	       && xmlnode_get_child(stanza, "body") != NULL
	       && xmlnode_get_attrib(stanza, "to") != NULL;
}

static void
sm_item_free(gpointer item)
{
	if (item != NULL)
		xmlnode_free(item);
}

/*
 * Tells the user about one unacked outgoing message (FALSE: not a message).
 * @a visible: write into a new conversation or a dialog when none is open
 * (connection lost, the user is looking).  At sign-off or quit the UI is
 * going away or the user left on purpose: only a conversation that already
 * exists gets the line, otherwise it goes to the debug log.
 */
static gboolean
sm_report_unacked(PurpleAccount *account, xmlnode *stanza, const char *fmt,
                  gboolean visible)
{
	const char *to, *type;
	PurpleConversation *conv = NULL;
	char *text, *bare, *escaped, *line;

	if (stanza == NULL || !sm_is_reportable(stanza))
		return FALSE;
	to = xmlnode_get_attrib(stanza, "to");
	type = xmlnode_get_attrib(stanza, "type");
	text = xmlnode_get_data(xmlnode_get_child(stanza, "body"));
	if (text == NULL)
		return FALSE;
	bare = jabber_get_bare_jid(to);
	if (purple_strequal(type, "groupchat")) {
		conv = purple_find_conversation_with_account(
			PURPLE_CONV_TYPE_CHAT, bare ? bare : to, account);
	} else {
		conv = purple_find_conversation_with_account(
			PURPLE_CONV_TYPE_IM, to, account);
		if (conv == NULL && bare != NULL)
			conv = purple_find_conversation_with_account(
				PURPLE_CONV_TYPE_IM, bare, account);
		if (conv == NULL && visible)
			conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account,
			                               bare ? bare : to);
	}
	escaped = g_markup_escape_text(text, -1);
	/* fmt is one of the _() literals of our callers, with one %s */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
	line = g_strdup_printf(fmt, escaped);
#pragma GCC diagnostic pop
	if (conv != NULL) {
		purple_conversation_write(conv, NULL, line,
		                          PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_ERROR,
		                          time(NULL));
	} else if (visible) {
		purple_debug_warning(SM_DBG, "no conversation for %s\n", to);
		purple_notify_warning(NULL, _("XMPP stream management"),
		                      _("Message may not have been delivered"), line);
	} else {
		purple_debug_warning(SM_DBG, "unacked at sign-off, no open "
		                     "conversation with %s\n", to);
	}
	g_free(line);
	g_free(escaped);
	g_free(bare);
	g_free(text);
	return TRUE;
}

/* Reports and forgets everything still unacked; @a fmt takes the text. */
static void
sm_report_queue(PurpleAccount *account, SmSession *s, const char *why,
                const char *fmt, gboolean visible)
{
	GList *items, *l;
	guint32 unacked;
	guint messages = 0;

	unacked = x2_sm_state_unacked(s->st);
	items = x2_sm_state_take_queue(s->st);
	for (l = items; l != NULL; l = l->next) {
		if (sm_report_unacked(account, l->data, fmt, visible))
			messages++;
	}
	g_list_free_full(items, sm_item_free);
	purple_debug_info(SM_DBG,
	                  "%s with %u unacked stanzas "
	                  "(%u messages reported, %u untracked)\n",
	                  why, unacked, messages, x2_sm_state_evicted(s->st));
	x2_sm_state_reset(s->st, 0, 0);
}

/* "connection-error": report and forget everything still unacked. */
static void
sm_connection_error_cb(PurpleConnection *gc, PurpleConnectionError reason,
                       const char *desc, gpointer unused)
{
	PurpleAccount *account = purple_connection_get_account(gc);
	SmSession *s = g_hash_table_lookup(sm_sessions, account);

	if (s == NULL)
		return;
	sm_report_queue(account, s, "connection lost",
		_("Connection lost: this message may not have been delivered: %s"),
		TRUE);
}

void
jabber_sm_init(void)
{
	sm_sessions = g_hash_table_new_full(g_direct_hash, g_direct_equal,
	                                    NULL, sm_session_free);
}

/* Prpls are loaded before the connection subsystem registers its signals,
 * so connect on first use instead of in jabber_sm_init(). */
static void
sm_connect_signals(void)
{
	if (sm_signal_connected)
		return;
	if (purple_signal_connect(purple_connections_get_handle(),
	                          "connection-error", &sm_signal_handle,
	                          PURPLE_CALLBACK(sm_connection_error_cb),
	                          NULL) == 0) {
		purple_debug_error(SM_DBG, "cannot connect to connection-error\n");
		return;
	}
	sm_signal_connected = TRUE;
}

void
jabber_sm_uninit(void)
{
	if (sm_signal_connected)
		purple_signals_disconnect_by_handle(&sm_signal_handle);
	sm_signal_connected = FALSE;
	g_hash_table_destroy(sm_sessions);
	sm_sessions = NULL;
}

void
jabber_sm_close(JabberStream *js)
{
	SmSession *s = sm_session_lookup(js);

	if (s == NULL)
		return;
	/* After a connection error the queue is already empty. */
	if (x2_sm_state_unacked(s->st) > 0)
		sm_report_queue(purple_connection_get_account(js->gc), s,
			"signed off",
			_("Signed off before the server confirmed this message, it "
			  "may not have been delivered: %s"), FALSE);
	g_hash_table_remove(sm_sessions, purple_connection_get_account(js->gc));
}

static void
sm_handle_enabled(JabberStream *js, SmSession *s, xmlnode *packet)
{
	const char *id = xmlnode_get_attrib(packet, "id");
	const char *max = xmlnode_get_attrib(packet, "max");
	const char *resume = xmlnode_get_attrib(packet, "resume");

	g_free(s->resume_id);
	g_free(s->resume_location);
	s->resume_id = g_strdup(id);
	s->resume_location = g_strdup(xmlnode_get_attrib(packet, "location"));
	s->resume_max = max ? (guint)strtoul(max, NULL, 10) : 0;
	s->resumable = purple_strequal(resume, "true")
	               || purple_strequal(resume, "1");
	js->sm_state = SM_ENABLED;
	purple_debug_info(SM_DBG, "enabled id=%s max=%u resume=%d location=%s\n",
	                  id ? id : "(none)", s->resume_max, s->resumable,
	                  s->resume_location ? s->resume_location : "(none)");
	if (x2_sm_state_need_request(s->st, 1))
		sm_arm_idle_request(js, s);
}

static void
sm_handle_failed(JabberStream *js, SmSession *s, xmlnode *packet)
{
	const char *h = xmlnode_get_attrib(packet, "h");
	guint trimmed = 0;
	guint dropped;

	if (h != NULL
	    && !x2_sm_state_ack(s->st, (guint32)strtoul(h, NULL, 10), &trimmed))
		purple_debug_error(SM_DBG, "<failed h='%s'> is beyond %u sent\n",
		                   h, x2_sm_state_outbound(s->st));
	/* No session: stanzas still queued went over a plain stream. */
	dropped = x2_sm_state_reset(s->st, 0, 0);
	purple_debug_error(SM_DBG, "failed h=%s: trimmed %u stanzas, "
	                   "dropped %u\n", h ? h : "(none)", trimmed, dropped);
	js->sm_state = SM_DISABLED;
}

static void
sm_handle_ack(JabberStream *js, SmSession *s, xmlnode *packet)
{
	const char *attr = xmlnode_get_attrib(packet, "h");
	guint32 h;
	guint trimmed;

	if (attr == NULL) {
		purple_debug_error(SM_DBG, "<a/> without 'h' ignored\n");
		return;
	}
	h = (guint32)strtoul(attr, NULL, 10);
	if (!x2_sm_state_ack(s->st, h, &trimmed)) {
		char *err = g_strdup_printf("<stream:error><undefined-condition "
			"xmlns='urn:ietf:params:xml:ns:xmpp-streams'/>"
			"<handled-count-too-high xmlns='%s' h='%u' send-count='%u'/>"
			"</stream:error>", NS_STREAM_MANAGEMENT, h,
			x2_sm_state_outbound(s->st));

		purple_debug_error(SM_DBG, "ack h=%u but only %u sent\n", h,
		                   x2_sm_state_outbound(s->st));
		jabber_send_raw(js, err, -1);
		g_free(err);
		purple_connection_error_reason(js->gc,
			PURPLE_CONNECTION_ERROR_NETWORK_ERROR,
			_("Server acknowledged more stanzas than were sent"));
		return;
	}
	purple_debug_info(SM_DBG, "ack h=%u: trimmed %u, %u unacked\n", h,
	                  trimmed, x2_sm_state_unacked(s->st));
}

/* <resumed/> or <failed/> we did not ask for: protocol error, disconnect. */
static void
sm_unexpected_reply(JabberStream *js, const char *name)
{
	const char *why = _("Unexpected stream management reply from the server");

	purple_debug_error(SM_DBG, "<%s/> without a <resume/> sent on this "
	                   "authenticated stream: protocol error\n", name);
	if (js->sm_resume != NULL)
		jabber_sm_resume_give_up(js, why);
	else
		purple_connection_error_reason(js->gc,
			PURPLE_CONNECTION_ERROR_NETWORK_ERROR, why);
}

/* Processes incoming NS_STREAM_MANAGEMENT packets. */
void
jabber_sm_process_packet(JabberStream *js, xmlnode *packet)
{
	const char *name = packet->name;
	SmSession *s = sm_session_lookup(js);

	if (s == NULL) {
		purple_debug_error(SM_DBG, "<%s/> outside a session ignored\n", name);
		return;
	}
	/* An answer to <resume/> is only valid after we sent one; a forged
	 * <resumed/> before SASL must not skip authentication. */
	if ((purple_strequal(name, "resumed")
	     || (purple_strequal(name, "failed") && js->sm_resume != NULL))
	    && !jabber_sm_resume_reply_ok(js)) {
		sm_unexpected_reply(js, name);
		return;
	}
	if (purple_strequal(name, "enabled"))
		sm_handle_enabled(js, s, packet);
	else if (purple_strequal(name, "resumed"))
		jabber_sm_resume_resumed(js, packet);
	else if (purple_strequal(name, "failed") && js->sm_resume != NULL)
		jabber_sm_resume_failed(js, packet);
	else if (purple_strequal(name, "failed"))
		sm_handle_failed(js, s, packet);
	else if (purple_strequal(name, "r"))
		jabber_sm_ack_send(js);
	else if (purple_strequal(name, "a"))
		sm_handle_ack(js, s, packet);
	else
		purple_debug_error(SM_DBG, "unknown packet <%s/>\n", name);
}

/* Sends an acknowledgement. */
void
jabber_sm_ack_send(JabberStream *js)
{
	SmSession *s = sm_session_lookup(js);
	xmlnode *ack;
	char *h;

	if (js->sm_state != SM_ENABLED || s == NULL || js->sm_resume != NULL)
		return;
	h = g_strdup_printf("%u", x2_sm_state_inbound(s->st));
	ack = xmlnode_new("a");
	xmlnode_set_namespace(ack, NS_STREAM_MANAGEMENT);
	xmlnode_set_attrib(ack, "h", h);
	jabber_send(js, ack);
	xmlnode_free(ack);
	g_free(h);
}

/* Asks the server to enable stream management for a fresh session. */
void
jabber_sm_enable(JabberStream *js)
{
	SmSession *s = sm_session_get(js);
	gboolean resume = purple_account_get_bool(
		purple_connection_get_account(js->gc), "sm_resume", TRUE);
	xmlnode *enable;
	guint stale;

	sm_connect_signals();
	js->server_caps |= JABBER_CAP_STREAM_MANAGEMENT;
	/* A new session never resends the queue of an earlier one: the server
	 * may have handled those stanzas already (ARCHITECTURE 7.8). */
	stale = x2_sm_state_reset(s->st, 0, 0);
	g_free(s->resume_id);
	s->resume_id = NULL;
	s->resumable = FALSE;
	purple_debug_info(SM_DBG, "enable requested (%s), fresh "
	                  "session, %u stale stanzas dropped, nothing resent\n",
	                  resume ? "resume" : "no resume", stale);
	enable = xmlnode_new("enable");
	xmlnode_set_namespace(enable, NS_STREAM_MANAGEMENT);
	if (resume)
		xmlnode_set_attrib(enable, "resume", "true");
	jabber_send(js, enable);
	xmlnode_free(enable);
	js->sm_state = SM_REQUESTED;
}

/* Counts and queues outbound stanzas, batches ack requests. */
void
jabber_sm_outbound(JabberStream *js, xmlnode *packet)
{
	SmSession *s;

	if (!jabber_is_stanza(packet)
	    || (js->sm_state != SM_REQUESTED && js->sm_state != SM_ENABLED))
		return;
	s = sm_session_lookup(js);
	g_return_if_fail(s != NULL);
	/* Counted from <enable/> on: the server counts from there too.  All
	 * are copied: a resumption sends every unacked stanza again. */
	x2_sm_state_sent(s->st, xmlnode_copy(packet));
	if (js->sm_state != SM_ENABLED)
		return;
	if (x2_sm_state_need_request(s->st, SM_REQUEST_EVERY))
		sm_send_request(js, s);
	else
		sm_arm_idle_request(js, s);
}

gboolean
jabber_sm_outbound_raw(JabberStream *js, const char *buf, int len)
{
	char *wrapped;
	xmlnode *root, *c;

	if (js->sm_state == SM_DISABLED)
		return TRUE;
	if (len < 0)
		len = strlen(buf);
	/* A wrapper element lets several top-level stanzas parse at once. */
	wrapped = g_strdup_printf("<x2raw>%.*s</x2raw>", len, buf);
	root = xmlnode_from_str(wrapped, -1);
	g_free(wrapped);
	if (root == NULL) {
		purple_debug_error(SM_DBG, "raw XML refused: not complete, "
		                   "well-formed elements\n");
		return FALSE;
	}
	for (c = root->child; c != NULL; c = c->next)
		if (c->type == XMLNODE_TYPE_TAG)
			jabber_sm_outbound(js, c);
	xmlnode_free(root);
	return TRUE;
}

/* Counts inbound stanzas (our h). */
void
jabber_sm_inbound(JabberStream *js, xmlnode *packet)
{
	SmSession *s;

	if (js->sm_state != SM_ENABLED || !jabber_is_stanza(packet))
		return;
	s = sm_session_lookup(js);
	if (s != NULL)
		x2_sm_state_received(s->st);
}

gboolean
jabber_sm_resumable(JabberStream *js, guint *max)
{
	SmSession *s = sm_session_lookup(js);

	if (s == NULL || js->sm_state != SM_ENABLED || !s->resumable
	    || s->resume_id == NULL)
		return FALSE;
	*max = s->resume_max;
	return TRUE;
}

X2SmState *
jabber_sm_get_state(JabberStream *js)
{
	SmSession *s = sm_session_lookup(js);

	return s ? s->st : NULL;
}

const char *
jabber_sm_get_resume_id(JabberStream *js)
{
	SmSession *s = sm_session_lookup(js);

	return s ? s->resume_id : NULL;
}

void
jabber_sm_request_ack(JabberStream *js)
{
	SmSession *s = sm_session_lookup(js);

	if (s != NULL && js->sm_state == SM_ENABLED)
		sm_send_request(js, s);
}
