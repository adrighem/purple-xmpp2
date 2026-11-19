/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file correct.c XEP-0308 Last Message Correction: inbound and outbound
 *
 * libpurple cannot edit a line that was written, so an accepted correction
 * is shown as a new line with the text prefixed by "corrected: " (translated)
 * and logged like any message.  A correction is accepted only if the message it
 * refers to was seen in the same conversation from the same author (bare JID
 * in 1:1, occupant-id or full JID in a room); anything else is shown as a
 * plain message, never as a correction (ARCHITECTURE 7.6, 9).
 */
#include "xmpp2_internal.h"

#include "debug.h"

#include "core/replace_parse.h"
#include "core/sid.h"
#include "ext/correct.h"
#include "ext/mam.h"
#include "jutil.h"
#include "namespaces.h"

#define DBG "xmpp2-correct"

X2CorrectState *
x2_correct_state(JabberStream *js)
{
	return js ? x2_ext_get_priv(js, &x2_ext_correct) : NULL;
}

static void
correct_stream_new(JabberStream *js)
{
	X2CorrectState *st = g_new0(X2CorrectState, 1);

	st->log = x2_corrlog_new(X2_CORRECT_LOG_SIZE);
	x2_ext_set_priv(js, &x2_ext_correct, st);
}

static void
correct_stream_free(JabberStream *js)
{
	X2CorrectState *st = x2_correct_state(js);

	if (st == NULL)
		return;
	x2_corrlog_free(st->log);
	g_free(st->pending);
	g_free(st);
	x2_ext_set_priv(js, &x2_ext_correct, NULL);
}

static void
correct_plugin_init(PurplePlugin *plugin)
{
	jabber_add_feature(NS_CORRECTION, NULL);
	x2_correct_cmd_register();
}

static void
correct_plugin_uninit(PurplePlugin *plugin)
{
	x2_correct_cmd_unregister();
	jabber_remove_feature(NS_CORRECTION);
}

char *
x2_correct_peer(JabberStream *js, const char *jid, gboolean groupchat)
{
	JabberID *id = jabber_id_new(jid);
	char *peer;

	if (id == NULL)
		return NULL;
	if (id->resource != NULL && id->node != NULL && !groupchat &&
	    jabber_chat_find(js, id->node, id->domain) != NULL)
		peer = g_strdup(jid);
	else
		peer = jabber_id_get_bare_jid(id);
	jabber_id_free(id);
	return peer;
}

/* Remembers the ids a sender may later refer to (id, origin-id, stanza-id) */
static void
record_ids(X2CorrLog *log, const X2Msg *m, const char *sender)
{
	const char *id = xmlnode_get_attrib(m->msg, "id");
	char *origin = x2_sid_origin_id(m->msg);

	if (id != NULL && *id != '\0')
		x2_corrlog_record(log, m->peer, id, sender);
	if (origin != NULL)
		x2_corrlog_record(log, m->peer, origin, sender);
	if (m->stanza_id != NULL)
		x2_corrlog_record(log, m->peer, m->stanza_id, sender);
	g_free(origin);
}

static void
note_original(X2CorrectState *st, const X2Msg *m, const char *sender)
{
	const char *id = xmlnode_get_attrib(m->msg, "id");

	record_ids(st->log, m, sender);
	/* a message from our other device is what /correct would fix here */
	if (m->dir == X2_DIR_OUT && id != NULL && *id != '\0')
		x2_corrlog_set_last_sent(st->log, m->peer, id);
}

static X2Verdict
correct_inbound(JabberStream *js, X2Msg *m)
{
	X2CorrectState *st = x2_correct_state(js);
	const char *type = xmlnode_get_attrib(m->msg, "type");
	char *target, *sender;
	X2CorrVerdict v;

	if (st == NULL || m->peer == NULL || purple_strequal(type, "error") ||
	    xmlnode_get_child(m->msg, "body") == NULL)
		return X2_PASS;

	/* an occupant-id counts only from a room that stamps and strips them */
	sender = x2_replace_sender(m->msg, xmlnode_get_attrib(m->msg, "from"),
	                           purple_strequal(type, "groupchat"),
	                           purple_strequal(type, "groupchat") &&
	                           x2_mam_room_has_occupant_id(js, m->archive));
	if (sender == NULL)
		return X2_PASS;

	target = x2_replace_id(m->msg);
	if (target == NULL) {
		note_original(st, m, sender);
		g_free(sender);
		return X2_PASS;
	}

	v = x2_corrlog_check(st->log, m->peer, target, sender);
	/* the msgid is X2_REPLACE_MARKER (core/ has no gettext) */
	if (v == X2_CORR_OK && x2_replace_mark_body(m->msg, _("corrected: "))) {
		purple_debug_info(DBG, "correction of %s accepted in %s\n",
		                  target, m->peer);
	} else {
		purple_debug_warning(DBG, "correction of %s from %s in %s "
		                     "shown as a plain message: %s\n", target,
		                     sender, m->peer,
		                     v == X2_CORR_MISMATCH ? "other author (forged)"
		                     : v == X2_CORR_UNKNOWN ? "original not seen"
		                     : "empty body");
	}
	g_free(target);
	g_free(sender);
	return X2_PASS;
}

static void
correct_outbound(JabberStream *js, xmlnode *message, X2Out *o)
{
	X2CorrectState *st = x2_correct_state(js);
	char *peer, *me;

	if (st == NULL || !o->has_body)
		return;

	if (st->pending != NULL) {
		/* a correction is not a new original: later corrections keep
		 * referring to the first message */
		x2_replace_add(message, st->pending);
		return;
	}

	peer = x2_correct_peer(js, xmlnode_get_attrib(message, "to"),
	                       o->groupchat);
	if (peer == NULL)
		return;
	x2_corrlog_set_last_sent(st->log, peer, o->id);
	if (!o->groupchat) {
		/* in a room our message comes back with its own author */
		me = jabber_id_get_bare_jid(js->user);
		x2_corrlog_record(st->log, peer, o->id, me);
		g_free(me);
	}
	g_free(peer);
}

const X2Ext x2_ext_correct = {
	.name = "correction",
	.plugin_init = correct_plugin_init,
	.plugin_uninit = correct_plugin_uninit,
	.stream_new = correct_stream_new,
	.stream_free = correct_stream_free,
	.inbound = correct_inbound,
	.outbound = correct_outbound,
};
