/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file carbons.c XEP-0280 message carbons (libpurple glue)
 *
 * server_info notes support, session_ready sends <enable/>, and the first
 * inbound module unwraps carbons through core/carbon_parse.c (ARCHITECTURE
 * 6 step 1, 7.1, 8).  After the enable IQ is answered, with a result or an
 * error, or at once when the server has no carbons, x2_ext_carbons_settled()
 * lets the MAM catch-up start: querying earlier would leave a window in
 * which a message from another device is neither archived for us yet nor
 * copied to us.
 */
#include "xmpp2_internal.h"

#include "debug.h"

#include "core/carbon_parse.h"
#include "ext/ext.h"
#include "iq.h"
#include "jutil.h"
#include "namespaces.h"

#define X2_DBG "xmpp2-carbons"

typedef struct {
	gboolean supported;   /* trusted disco#info advertised the namespace */
	gboolean settled;     /* x2_ext_carbons_settled() was called */
} CarbonsState;

extern const X2Ext x2_ext_carbons;

static CarbonsState *
state_get(JabberStream *js)
{
	return x2_ext_get_priv(js, &x2_ext_carbons);
}

static void
carbons_stream_new(JabberStream *js)
{
	x2_ext_set_priv(js, &x2_ext_carbons, g_new0(CarbonsState, 1));
}

static void
carbons_stream_free(JabberStream *js)
{
	g_free(state_get(js));
	x2_ext_set_priv(js, &x2_ext_carbons, NULL);
}

/* Once per stream, whatever happened to the enable request. */
static void
settle(JabberStream *js, CarbonsState *st)
{
	if (st->settled)
		return;
	st->settled = TRUE;
	x2_ext_carbons_settled(js);
}

/*
 * Carbons are a server feature: believe our server domain or our own account
 * (Prosody lists account features on the bare JID), never a component or a
 * foreign service.
 */
static void
carbons_server_info(JabberStream *js, const char *from, xmlnode *query)
{
	CarbonsState *st = state_get(js);
	char *bare = jabber_id_get_bare_jid(js->user);
	xmlnode *f;

	if (!purple_strequal(from, js->user->domain) &&
	    !purple_strequal(from, bare)) {
		g_free(bare);
		return;
	}
	g_free(bare);

	for (f = xmlnode_get_child(query, "feature"); f;
	     f = xmlnode_get_next_twin(f))
		if (purple_strequal(xmlnode_get_attrib(f, "var"), NS_CARBONS)) {
			st->supported = TRUE;
			return;
		}
}

static void
enable_result_cb(JabberStream *js, const char *from, JabberIqType type,
                 const char *id, xmlnode *packet, gpointer data)
{
	CarbonsState *st = state_get(js);

	if (st == NULL)
		return;
	if (type == JABBER_IQ_RESULT)
		purple_debug_info(X2_DBG, "carbons enabled\n");
	else
		purple_debug_warning(X2_DBG, "carbons enable failed, "
		                     "continuing without\n");
	settle(js, st);
}

static void
carbons_session_ready(JabberStream *js)
{
	CarbonsState *st = state_get(js);
	JabberIq *iq;
	xmlnode *enable;

	if (st->settled)
		return;
	if (!st->supported) {
		purple_debug_info(X2_DBG, "server has no carbons, not enabling\n");
		settle(js, st);
		return;
	}

	iq = jabber_iq_new(js, JABBER_IQ_SET);
	enable = xmlnode_new_child(iq->node, "enable");
	xmlnode_set_namespace(enable, NS_CARBONS);
	jabber_iq_set_callback(iq, enable_result_cb, NULL);
	purple_debug_info(X2_DBG, "enabling carbons\n");
	jabber_iq_send(iq);
}

static X2Verdict
carbons_inbound(JabberStream *js, X2Msg *m)
{
	xmlnode *inner;
	const char *reason;
	char *bare;
	X2CarbonKind kind;

	/* Only a stanza fresh from the parser can be a carbon */
	if (m->src != X2_SRC_LIVE || m->msg != m->outer)
		return X2_PASS;

	bare = jabber_id_get_bare_jid(js->user);
	kind = x2_carbon_classify(m->outer, bare, &inner, &reason);
	g_free(bare);

	switch (kind) {
	case X2_CARBON_NONE:
		return X2_PASS;
	case X2_CARBON_REJECTED:
		purple_debug_warning(X2_DBG, "forged carbon dropped: %s "
		                     "(outer from=%s)\n", reason,
		                     xmlnode_get_attrib(m->outer, "from")
		                     ? xmlnode_get_attrib(m->outer, "from")
		                     : "-");
		return X2_CONSUMED;
	case X2_CARBON_SENT:
		x2_msg_set_inner(js, m, inner, X2_SRC_CARBON, X2_DIR_OUT, NULL);
		purple_debug_info(X2_DBG, "sent carbon accepted, peer %s\n",
		                  m->peer ? m->peer : "-");
		return X2_PASS;
	case X2_CARBON_RECEIVED:
		x2_msg_set_inner(js, m, inner, X2_SRC_CARBON, X2_DIR_IN, NULL);
		purple_debug_info(X2_DBG, "received carbon accepted, peer %s\n",
		                  m->peer ? m->peer : "-");
		return X2_PASS;
	}
	return X2_PASS;
}

const X2Ext x2_ext_carbons = {
	.name = "carbons",
	.stream_new = carbons_stream_new,
	.stream_free = carbons_stream_free,
	.server_info = carbons_server_info,
	.session_ready = carbons_session_ready,
	.inbound = carbons_inbound,
};
