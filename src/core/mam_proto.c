/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file mam_proto.c XEP-0313 Message Archive Management (pure logic)
 */
#include <string.h>

#include "mam_proto.h"
#include "forward.h"
#include "jid.h"

#define X2_NS_XDATA "jabber:x:data"
#define X2_NS_STANZAS "urn:ietf:params:xml:ns:xmpp-stanzas"

static void
add_field(xmlnode *x, const char *var, const char *type, const char *value)
{
	xmlnode *field = xmlnode_new_child(x, "field");
	xmlnode *v;

	xmlnode_set_attrib(field, "var", var);
	if (type != NULL)
		xmlnode_set_attrib(field, "type", type);
	v = xmlnode_new_child(field, "value");
	xmlnode_insert_data(v, value, -1);
}

static void
add_text_child(xmlnode *parent, const char *name, const char *text)
{
	xmlnode *n = xmlnode_new_child(parent, name);

	if (text != NULL && *text != '\0')
		xmlnode_insert_data(n, text, -1);
}

xmlnode *
x2_mam_query_new(const X2MamQuery *q)
{
	xmlnode *query, *x, *set;

	g_return_val_if_fail(q != NULL, NULL);
	g_return_val_if_fail(q->queryid != NULL && *q->queryid != '\0', NULL);
	g_return_val_if_fail(!(q->after != NULL && q->last_page), NULL);

	query = xmlnode_new("query");
	xmlnode_set_namespace(query, X2_NS_MAM);
	xmlnode_set_attrib(query, "queryid", q->queryid);

	x = xmlnode_new_child(query, "x");
	xmlnode_set_namespace(x, X2_NS_XDATA);
	xmlnode_set_attrib(x, "type", "submit");
	add_field(x, "FORM_TYPE", "hidden", X2_NS_MAM);
	if (q->with != NULL)
		add_field(x, "with", NULL, q->with);
	if (q->id != NULL) {
		xmlnode *field = xmlnode_new_child(x, "field");

		xmlnode_set_attrib(field, "var", "ids");
		xmlnode_set_attrib(field, "type", "list-multi");
		xmlnode_insert_data(xmlnode_new_child(field, "value"), q->id, -1);
	}
	if (q->start > 0) {
		char stamp[32];
		struct tm tm;

		if (gmtime_r(&q->start, &tm) == NULL ||
		    strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &tm) == 0) {
			g_critical("x2_mam_query_new: bad start time");
			xmlnode_free(query);
			return NULL;
		}
		add_field(x, "start", NULL, stamp);
	}

	set = xmlnode_new_child(query, "set");
	xmlnode_set_namespace(set, X2_NS_RSM);
	if (q->max > 0) {
		char *max = g_strdup_printf("%u", q->max);

		add_text_child(set, "max", max);
		g_free(max);
	}
	if (q->after != NULL)
		add_text_child(set, "after", q->after);
	else if (q->last_page)
		add_text_child(set, "before", NULL);

	return query;
}

gboolean
x2_mam_inner_is_own(const xmlnode *inner, const char *own_bare)
{
	g_return_val_if_fail(inner != NULL, FALSE);
	g_return_val_if_fail(own_bare != NULL, FALSE);

	return x2_jid_bare_equal(xmlnode_get_attrib(inner, "from"), own_bare) ||
	       x2_jid_bare_equal(xmlnode_get_attrib(inner, "to"), own_bare);
}

/* Checks shared by both archive kinds, after the outer from: queryid,
 * result id and a forwarded message. */
static X2MamCheck
check_result(xmlnode *result, const char *queryid, xmlnode **inner,
             const char **id, const char **why)
{
	xmlnode *msg;
	const char *rid;

	if (queryid == NULL ||
	    g_strcmp0(xmlnode_get_attrib(result, "queryid"), queryid) != 0) {
		*why = "queryid does not match an active query";
		return X2_MAM_FORGED;
	}
	rid = xmlnode_get_attrib(result, "id");
	if (rid == NULL || *rid == '\0') {
		*why = "result without id";
		return X2_MAM_FORGED;
	}
	msg = x2_forward_get_message(result);
	if (msg == NULL) {
		*why = "result without forwarded message";
		return X2_MAM_FORGED;
	}
	*inner = msg;
	*id = rid;
	return X2_MAM_VALID;
}

X2MamCheck
x2_mam_result_check(xmlnode *outer, const char *own_bare,
                    const char *queryid, xmlnode **inner,
                    const char **id, const char **why)
{
	xmlnode *result;
	const char *from;

	g_return_val_if_fail(outer != NULL, X2_MAM_NOT_RESULT);
	g_return_val_if_fail(own_bare != NULL, X2_MAM_NOT_RESULT);
	g_return_val_if_fail(inner != NULL && id != NULL && why != NULL,
	                     X2_MAM_NOT_RESULT);

	*inner = NULL;
	*id = NULL;
	*why = NULL;

	result = xmlnode_get_child_with_namespace(outer, "result", X2_NS_MAM);
	if (result == NULL)
		return X2_MAM_NOT_RESULT;

	/* The account archive answers from our bare JID, or without a from
	 * (the server itself on our behalf).  A full JID is not the archive. */
	from = xmlnode_get_attrib(outer, "from");
	if (from != NULL && strcmp(from, own_bare) != 0) {
		*why = "outer from is not our archive";
		return X2_MAM_FORGED;
	}
	if (check_result(result, queryid, inner, id, why) != X2_MAM_VALID)
		return X2_MAM_FORGED;
	if (!x2_mam_inner_is_own(*inner, own_bare)) {
		*inner = NULL;
		*id = NULL;
		*why = "inner message is not to or from us";
		return X2_MAM_FORGED;
	}
	return X2_MAM_VALID;
}

X2MamCheck
x2_mam_room_result_check(xmlnode *outer, const char *room,
                         const char *queryid, xmlnode **inner,
                         const char **id, const char **why)
{
	xmlnode *result;

	g_return_val_if_fail(outer != NULL, X2_MAM_NOT_RESULT);
	g_return_val_if_fail(room != NULL, X2_MAM_NOT_RESULT);
	g_return_val_if_fail(inner != NULL && id != NULL && why != NULL,
	                     X2_MAM_NOT_RESULT);

	*inner = NULL;
	*id = NULL;
	*why = NULL;

	result = xmlnode_get_child_with_namespace(outer, "result", X2_NS_MAM);
	if (result == NULL)
		return X2_MAM_NOT_RESULT;

	/* Only the room itself answers for its archive: an occupant
	 * (room/nick) or anybody else is not the archive. */
	if (g_strcmp0(xmlnode_get_attrib(outer, "from"), room) != 0) {
		*why = "outer from is not the room";
		return X2_MAM_FORGED;
	}
	if (check_result(result, queryid, inner, id, why) != X2_MAM_VALID)
		return X2_MAM_FORGED;
	if (!x2_jid_bare_equal(xmlnode_get_attrib(*inner, "from"), room)) {
		*inner = NULL;
		*id = NULL;
		*why = "inner message is not from the room";
		return X2_MAM_FORGED;
	}
	/* Whatever the archive says, it is a message of this room: never a
	 * 1:1 conversation with an occupant or a spoofed JID. */
	xmlnode_set_attrib(*inner, "type", "groupchat");
	return X2_MAM_VALID;
}

X2MamRoomStart
x2_mam_room_start(const char *cursor, gint64 cursor_ts, time_t now,
                  int threshold_days)
{
	if (cursor == NULL || *cursor == '\0' || cursor_ts <= 0 ||
	    threshold_days <= 0)
		return X2_MAM_ROOM_LAST_PAGE;
	if ((gint64)now - cursor_ts > (gint64)threshold_days * 24 * 3600)
		return X2_MAM_ROOM_LAST_PAGE;
	return X2_MAM_ROOM_AFTER_CURSOR;
}

static char *
child_text(const xmlnode *parent, const char *name)
{
	xmlnode *n = xmlnode_get_child(parent, name);
	char *s;

	if (n == NULL)
		return NULL;
	s = xmlnode_get_data(n);
	if (s != NULL && *s == '\0') {
		g_free(s);
		return NULL;
	}
	return s;
}

X2MamQueued
x2_mam_queued_fate(gboolean seen, gboolean complete, gboolean last_page,
                   gboolean delayed, gboolean has_stanza_id)
{
	if (seen)
		return X2_MAM_QUEUED_DUPLICATE;
	if (complete && last_page && delayed && has_stanza_id)
		return X2_MAM_QUEUED_SKIPPED;
	return X2_MAM_QUEUED_DELIVER;
}

gboolean
x2_mam_fin_parse(const xmlnode *iq, X2MamFin *fin)
{
	xmlnode *f, *set;
	const char *complete;

	g_return_val_if_fail(iq != NULL, FALSE);
	g_return_val_if_fail(fin != NULL, FALSE);

	memset(fin, 0, sizeof(*fin));

	if (g_strcmp0(iq->name, "fin") == 0 &&
	    g_strcmp0(xmlnode_get_namespace((xmlnode *)iq), X2_NS_MAM) == 0)
		f = (xmlnode *)iq;
	else
		f = xmlnode_get_child_with_namespace(iq, "fin", X2_NS_MAM);
	if (f == NULL)
		return FALSE;

	/* xs:boolean: "true" or "1" */
	complete = xmlnode_get_attrib(f, "complete");
	fin->complete = g_strcmp0(complete, "true") == 0 ||
	                g_strcmp0(complete, "1") == 0;
	set = xmlnode_get_child_with_namespace(f, "set", X2_NS_RSM);
	if (set != NULL) {
		fin->first = child_text(set, "first");
		fin->last = child_text(set, "last");
	}
	return TRUE;
}

void
x2_mam_fin_clear(X2MamFin *fin)
{
	if (fin == NULL)
		return;
	g_free(fin->first);
	g_free(fin->last);
	memset(fin, 0, sizeof(*fin));
}

gboolean
x2_mam_error_is_item_not_found(const xmlnode *iq)
{
	xmlnode *error;

	g_return_val_if_fail(iq != NULL, FALSE);

	error = xmlnode_get_child(iq, "error");
	return error != NULL &&
	       xmlnode_get_child_with_namespace(error, "item-not-found",
	                                        X2_NS_STANZAS) != NULL;
}
