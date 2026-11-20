/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/*
 * XEP-0198 resumption as a silent reconnect, see sm_resume.h and
 * docs/ARCHITECTURE.md 7.8.  The protocol side (<resume/>, <resumed/>,
 * <failed/>, the resend) is stream_management.c; this file owns the
 * transport lifecycle: teardown, delays, attempts and their timeouts.
 *
 * Every timer belongs to the JabberSmResume of one JabberStream and is
 * removed by jabber_sm_resume_cancel() from jabber_close(), so no
 * callback outlives the connection.
 */
#include "xmpp2_internal.h"

#include "debug.h"
#include "eventloop.h"

#include "auth.h"
#include "jabber.h"
#include "namespaces.h"
#include "presence.h"
#include "stream_management.h"
#include "sm_resume.h"
#include "smresume.h"
#include "smstate.h"

#define SM_DBG "xmpp2-sm"

#ifdef XMPP2_TEST_HOOKS
/* Test hook (cmake -DXMPP2_TEST_HOOKS=ON only): delay before the first
 * attempt in ms, so a test can act while the transport is down
 * (tests/integration/test_sm_resume.sh). */
#define SM_RESUME_DELAY_ENV "XMPP2_TEST_SM_RESUME_DELAY_MS"
#endif

typedef struct _JabberSmResume JabberSmResume;

struct _JabberSmResume {
	X2SmResume policy;
	guint timer;		/* teardown, then the delay before an attempt */
	guint attempt_timer;	/* the running attempt's timeout */
	gint next_delay;	/* ms, for the timer after the teardown */
	gboolean gave_up;	/* connection error raised, waiting for close */
};

static gint64
now_us(void)
{
	return g_get_monotonic_time();
}

static guint
first_delay_ms(void)
{
#ifdef XMPP2_TEST_HOOKS
	const char *env = g_getenv(SM_RESUME_DELAY_ENV);

	if (env != NULL && atoi(env) > 0)
		return (guint)atoi(env);
#endif
	return X2_SM_RESUME_FIRST_DELAY_MS;
}

static void
remove_timers(JabberSmResume *r)
{
	if (r->timer) {
		purple_timeout_remove(r->timer);
		r->timer = 0;
	}
	if (r->attempt_timer) {
		purple_timeout_remove(r->attempt_timer);
		r->attempt_timer = 0;
	}
}

static gboolean attempt_cb(gpointer data);

/* Deferred: the failure may be reported from inside the parser. */
static gboolean
teardown_cb(gpointer data)
{
	JabberStream *js = data;
	JabberSmResume *r = js->sm_resume;

	r->timer = 0;
	jabber_transport_close(js);
	purple_debug_info(SM_DBG, "transport closed, attempt %u of %u in "
	                  "%d ms\n", r->policy.attempts + 1,
	                  X2_SM_RESUME_ATTEMPTS, r->next_delay);
	r->timer = purple_timeout_add(r->next_delay, attempt_cb, js);
	return FALSE;
}

static void
schedule_teardown(JabberStream *js, gint delay)
{
	JabberSmResume *r = js->sm_resume;

	js->transport_down = TRUE;
	remove_timers(r);
	r->next_delay = delay;
	r->timer = purple_timeout_add(0, teardown_cb, js);
}

static gboolean
attempt_timeout_cb(gpointer data)
{
	JabberStream *js = data;

	js->sm_resume->attempt_timer = 0;
	jabber_stream_lost(js, _("Stream resumption timed out"));
	return FALSE;
}

static gboolean
attempt_cb(gpointer data)
{
	JabberStream *js = data;
	JabberSmResume *r = js->sm_resume;

	r->timer = 0;
	if (!x2_sm_resume_start(&r->policy, now_us())) {
		jabber_sm_resume_give_up(js,
			_("Lost connection with server: the session expired"));
		return FALSE;
	}
	purple_debug_info(SM_DBG, "resume attempt %u of %u: reconnecting\n",
	                  r->policy.attempts, X2_SM_RESUME_ATTEMPTS);
	js->transport_down = FALSE;
	r->attempt_timer = purple_timeout_add(X2_SM_RESUME_ATTEMPT_TIMEOUT_MS,
	                                      attempt_timeout_cb, js);
	/* May fail at once and come back through jabber_stream_lost(). */
	jabber_stream_connect(js);
	return FALSE;
}

/* A failure while an attempt runs, or a second report of the same one. */
static void
attempt_failed(JabberStream *js, const char *why)
{
	JabberSmResume *r = js->sm_resume;
	gint delay;

	if (r->gave_up || js->transport_down)
		return;
	delay = x2_sm_resume_failed(&r->policy, now_us());
	purple_debug_warning(SM_DBG, "resume attempt %u failed: %s\n",
	                     r->policy.attempts, why ? why : "");
	if (delay < 0) {
		char *msg = g_strdup_printf(_("%s (stream resumption failed)"),
		                            why ? why : _("Lost connection"));

		jabber_sm_resume_give_up(js, msg);
		g_free(msg);
		return;
	}
	schedule_teardown(js, delay);
}

gboolean
jabber_sm_resume_lost(JabberStream *js, const char *why)
{
	PurpleConnection *gc = js->gc;
	PurpleAccount *account = purple_connection_get_account(gc);
	JabberSmResume *r = js->sm_resume;
	guint max = 0;
	gint delay;

	if (r != NULL) {
		attempt_failed(js, why);
		return TRUE;
	}
	/* Only a session that is fully up and that the server keeps. */
	if (account->disconnecting || gc->disconnect_timeout
	    || purple_connection_get_state(gc) != PURPLE_CONNECTED
	    || js->state != JABBER_STREAM_CONNECTED
	    || !jabber_sm_resumable(js, &max))
		return FALSE;

	r = g_new0(JabberSmResume, 1);
	js->sm_resume = r;
	delay = x2_sm_resume_lost(&r->policy, TRUE, max, first_delay_ms(),
	                          now_us());
	purple_debug_info(SM_DBG, "transport lost (%s): resuming within %u s\n",
	                  why ? why : "", x2_sm_resume_window(max));
	schedule_teardown(js, delay);
	return TRUE;
}

void
jabber_sm_resume_features(JabberStream *js, xmlnode *packet)
{
	if (js->sm_resume->gave_up)
		return;
	if (xmlnode_get_child(packet, "mechanisms") != NULL) {
		jabber_stream_set_state(js, JABBER_STREAM_AUTHENTICATING);
		jabber_auth_start(js, packet);
	} else if (js->state == JABBER_STREAM_POST_AUTH
	           && xmlnode_get_child_with_namespace(packet, "sm",
	                                               NS_STREAM_MANAGEMENT)) {
		jabber_sm_send_resume(js);
	} else {
		jabber_sm_resume_give_up(js, _("Lost connection with server: "
			"stream resumption not offered"));
	}
}

static void
resume_done(JabberStream *js)
{
	JabberSmResume *r = js->sm_resume;

	g_return_if_fail(r != NULL);
	remove_timers(r);
	g_free(r);
	js->sm_resume = NULL;
	js->transport_down = FALSE;
	js->state = JABBER_STREAM_CONNECTED;
	jabber_stream_restart_inactivity_timer(js);
}

void
jabber_sm_send_resume(JabberStream *js)
{
	X2SmState *st = jabber_sm_get_state(js);
	const char *id = jabber_sm_get_resume_id(js);
	xmlnode *resume;
	char *h;

	if (st == NULL || id == NULL) {
		jabber_sm_resume_give_up(js, _("Lost connection with server"));
		return;
	}
	h = g_strdup_printf("%u", x2_sm_state_inbound(st));
	resume = xmlnode_new("resume");
	xmlnode_set_namespace(resume, NS_STREAM_MANAGEMENT);
	xmlnode_set_attrib(resume, "previd", id);
	xmlnode_set_attrib(resume, "h", h);
	purple_debug_info(SM_DBG, "resume previd=%s h=%s, %u unacked\n", id, h,
	                  x2_sm_state_unacked(st));
	jabber_send(js, resume);
	xmlnode_free(resume);
	g_free(h);
}

void
jabber_sm_resume_resumed(JabberStream *js, xmlnode *packet)
{
	X2SmState *st = jabber_sm_get_state(js);
	const char *hs = xmlnode_get_attrib(packet, "h");
	const char *previd = xmlnode_get_attrib(packet, "previd");
	GList *resend = NULL, *l;
	guint trimmed = 0, n = 0;
	X2SmResend res;

	if (st == NULL || hs == NULL
	    || !purple_strequal(previd, jabber_sm_get_resume_id(js))) {
		purple_debug_error(SM_DBG, "<resumed previd='%s' h='%s'/> does "
		                   "not match our session\n",
		                   previd ? previd : "", hs ? hs : "");
		jabber_sm_resume_give_up(js,
			_("Lost connection with server: invalid stream resumption"));
		return;
	}
	res = x2_sm_state_resumed(st, (guint32)strtoul(hs, NULL, 10), &trimmed,
	                          &resend);
	if (res != X2_SM_RESEND_OK) {
		purple_debug_error(SM_DBG, "<resumed h='%s'/>: %s\n", hs,
			res == X2_SM_RESEND_H_TOO_HIGH
			? "acknowledges more than was sent"
			: "unacked stanzas were not kept");
		jabber_sm_resume_give_up(js,
			_("Lost connection with server: invalid stream resumption"));
		return;
	}
	/* Back to normal sending before the resend; the resend keeps its
	 * sequence numbers, so it bypasses the counting send path. */
	resume_done(js);
	for (l = resend; l != NULL; l = l->next) {
		int len;
		char *txt = xmlnode_to_str(l->data, &len);

		jabber_send_raw(js, txt, len);
		g_free(txt);
		n++;
	}
	g_list_free(resend);
	purple_debug_info(SM_DBG, "resumed previd=%s h=%s: %u acked, %u "
	                  "resent\n", previd, hs, trimmed, n);
	if (n > 0)
		jabber_sm_request_ack(js);
	/* A status change during the gap was not sent (no roster then);
	 * after the resend, so the wire order matches the counters. */
	jabber_presence_send(js, FALSE);
}

void
jabber_sm_resume_failed(JabberStream *js, xmlnode *packet)
{
	X2SmState *st = jabber_sm_get_state(js);
	const char *hs = xmlnode_get_attrib(packet, "h");
	const char *cond = "(none)";
	guint trimmed = 0;
	xmlnode *c;

	for (c = packet->child; c != NULL; c = c->next) {
		if (c->type == XMLNODE_TYPE_TAG) {
			cond = c->name;
			break;
		}
	}
	/* XEP-0198 1.6: h on <failed/> says what the old session handled */
	if (hs != NULL && st != NULL
	    && !x2_sm_state_ack(st, (guint32)strtoul(hs, NULL, 10), &trimmed))
		purple_debug_error(SM_DBG, "<failed h='%s'> is beyond %u sent\n",
		                   hs, x2_sm_state_outbound(st));
	purple_debug_warning(SM_DBG, "resume failed (%s) h=%s: %u acked, %u "
	                     "unacked\n", cond, hs ? hs : "(none)", trimmed,
	                     st ? x2_sm_state_unacked(st) : 0);
	jabber_sm_resume_give_up(js,
		_("Lost connection with server: the session could not be "
		  "resumed"));
}

void
jabber_sm_resume_give_up(JabberStream *js, const char *why)
{
	JabberSmResume *r = js->sm_resume;

	g_return_if_fail(r != NULL);
	if (r->gave_up)
		return;
	/* Keep r: stanzas stay held (and reported) until the close. */
	remove_timers(r);
	r->gave_up = TRUE;
	x2_sm_resume_reset(&r->policy);
	purple_debug_warning(SM_DBG, "resume given up: %s; full reconnect\n",
	                     why);
	purple_connection_error_reason(js->gc,
		PURPLE_CONNECTION_ERROR_NETWORK_ERROR, why);
}

void
jabber_sm_resume_cancel(JabberStream *js)
{
	JabberSmResume *r = js->sm_resume;

	if (r == NULL)
		return;
	if (!r->gave_up)
		purple_debug_info(SM_DBG, "closed while resuming: not resumed\n");
	remove_timers(r);
	g_free(r);
	js->sm_resume = NULL;
	/* The new stream (if any) has no SM session: no <a/> on close. */
	js->sm_state = SM_DISABLED;
}
