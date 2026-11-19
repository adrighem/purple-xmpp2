/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file mam_muc.c XEP-0313 for rooms (ARCHITECTURE 7.2, M4)
 *
 * Join: the room's disco#info is fetched before the join presence (cached
 * per stream; the join waits at most X2_MAM_ROOM_DISCO_SECS).  A room that
 * advertises urn:xmpp:mam:2 is joined with <history maxchars='0'
 * maxstanzas='0'/> and its stanza-ids are trusted (also for sid:0).  On our
 * first self-presence (chat_joined, once per join) the room archive is
 * caught up from its cursor, or the last page only when there is none or it
 * is older than mam_muc_threshold_days.  Live messages of the room wait
 * while it syncs (mam.c), the state machine is mam_sync.c.
 */
#include "xmpp2_internal.h"

#include "accountopt.h"
#include "debug.h"
#include "eventloop.h"

#include "core/mam_proto.h"
#include "ext/mam.h"
#include "iq.h"
#include "jutil.h"
#include "namespaces.h"

/* Seconds a join waits for the room's disco#info before going ahead */
#define X2_MAM_ROOM_DISCO_SECS 5

/* Default of the account option mam_muc_threshold_days */
#define X2_MAM_ROOM_THRESHOLD_DAYS 2

/* A join presence held until the room's disco#info answers */
typedef struct {
	X2MamState *st;
	char *room;
	guint timer;
} HeldJoin;

static char *
room_jid(JabberChat *chat)
{
	return g_strdup_printf("%s@%s", chat->room, chat->server);
}

static int
room_flags(X2MamState *st, const char *room)
{
	return GPOINTER_TO_INT(g_hash_table_lookup(st->room_info, room));
}

static void
held_join_free(gpointer data)
{
	HeldJoin *h = data;

	if (h->timer != 0)
		purple_timeout_remove(h->timer);
	g_free(h->room);
	g_free(h);
}

/* Sends the held join presence of @a room, unless the user gave up on it */
static void
release_join(X2MamState *st, const char *room)
{
	JabberID *jid = jabber_id_new(room);
	JabberChat *chat;

	g_hash_table_remove(st->held_joins, room);
	if (jid == NULL)
		return;
	chat = jabber_chat_find(st->js, jid->node, jid->domain);
	if (chat != NULL && chat->conv == NULL && !chat->left)
		jabber_chat_send_join(chat);
	jabber_id_free(jid);
}

static gboolean
held_join_timeout(gpointer data)
{
	HeldJoin *h = data;
	X2MamState *st = h->st;
	char *room = g_strdup(h->room);

	h->timer = 0;
	purple_debug_warning(X2_MAM_DEBUG, "room %s: no disco#info answer, "
	                     "joining without MAM\n", room);
	release_join(st, room);
	g_free(room);
	return FALSE;
}

static void
room_info_cb(JabberStream *js, const char *from, JabberIqType type,
             const char *id, xmlnode *packet, gpointer data)
{
	X2MamState *st = x2_mam_state(js);
	xmlnode *query = NULL;
	int flags = X2_MAM_ROOM_KNOWN;
	char *room;

	/* Unknown room or timed out: the join went ahead already */
	if (st == NULL || from == NULL ||
	    !g_hash_table_contains(st->held_joins, from))
		return;
	room = g_strdup(from);

	if (type == JABBER_IQ_RESULT)
		query = xmlnode_get_child_with_namespace(packet, "query",
		                                         NS_DISCO_INFO);
	/* An error (e.g. a room that does not exist yet) is not cached: the
	 * next join asks again. */
	if (query != NULL) {
		if (x2_mam_has_feature(query, NS_MAM))
			flags |= X2_MAM_ROOM_MAM;
		if (x2_mam_has_feature(query, X2_NS_MAM_EXTENDED))
			flags |= X2_MAM_ROOM_EXTENDED;
		/* The room stamps and strips stanza-ids by=room */
		if ((flags & X2_MAM_ROOM_MAM) || x2_mam_has_feature(query, NS_SID))
			x2_ext_archive_trust(js, room);
		g_hash_table_replace(st->room_info, g_strdup(room),
		                     GINT_TO_POINTER(flags));
	}
	purple_debug_info(X2_MAM_DEBUG, "room %s: %s\n", room,
	                  (flags & X2_MAM_ROOM_MAM) ? "MAM" : "no MAM");
	release_join(st, room);
	g_free(room);
}

gboolean
x2_mam_muc_chat_joining(JabberStream *js, JabberChat *chat)
{
	X2MamState *st = x2_mam_state(js);
	JabberIq *iq;
	HeldJoin *h;
	char *room;

	if (st == NULL)
		return FALSE;
	room = room_jid(chat);
	if (g_hash_table_contains(st->room_info, room) ||
	    g_hash_table_contains(st->held_joins, room)) {
		g_free(room);
		return FALSE;
	}

	iq = jabber_iq_new_query(js, JABBER_IQ_GET, NS_DISCO_INFO);
	xmlnode_set_attrib(iq->node, "to", room);
	jabber_iq_set_callback(iq, room_info_cb, NULL);

	h = g_new0(HeldJoin, 1);
	h->st = st;
	h->room = room;
	h->timer = purple_timeout_add_seconds(X2_MAM_ROOM_DISCO_SECS,
	                                      held_join_timeout, h);
	g_hash_table_replace(st->held_joins, g_strdup(room), h);
	purple_debug_info(X2_MAM_DEBUG, "room %s: disco#info before the join\n",
	                  room);
	jabber_iq_send(iq);
	return TRUE;
}

void
x2_mam_muc_join_presence(JabberStream *js, JabberChat *chat, xmlnode *muc_x)
{
	X2MamState *st = x2_mam_state(js);
	xmlnode *history;
	char *room;

	if (st == NULL)
		return;
	room = room_jid(chat);
	if (room_flags(st, room) & X2_MAM_ROOM_MAM) {
		/* The archive replaces legacy history; some servers honour
		 * only one of the two limits. */
		history = xmlnode_get_child(muc_x, "history");
		if (history == NULL)
			history = xmlnode_new_child(muc_x, "history");
		xmlnode_remove_attrib(history, "seconds");
		xmlnode_remove_attrib(history, "since");
		xmlnode_set_attrib(history, "maxchars", "0");
		xmlnode_set_attrib(history, "maxstanzas", "0");
		purple_debug_info(X2_MAM_DEBUG, "room %s: joining without legacy "
		                  "history\n", room);
	}
	g_free(room);
}

void
x2_mam_muc_first_query(X2MamArchive *a)
{
	PurpleAccount *account = purple_connection_get_account(a->st->js->gc);
	int days = purple_account_get_int(account, "mam_muc_threshold_days",
	                                  X2_MAM_ROOM_THRESHOLD_DAYS);
	gint64 ts = 0;
	const char *cursor = x2_store_get_cursor(x2_mam_store(a->st), a->jid,
	                                         &ts);

	if (x2_mam_room_start(cursor, ts, time(NULL), days) ==
	    X2_MAM_ROOM_AFTER_CURSOR) {
		a->from_cursor = TRUE;
		x2_mam_sync_query(a, X2_MAMQ_FORWARD, cursor, 0, NULL);
	} else
		x2_mam_sync_query(a, X2_MAMQ_LAST, NULL, 0, NULL);
}

void
x2_mam_muc_chat_joined(JabberStream *js, JabberChat *chat)
{
	X2MamState *st = x2_mam_state(js);
	X2MamArchive *a;
	char *room;
	int flags;

	if (st == NULL)
		return;
	room = room_jid(chat);
	flags = room_flags(st, room);
	if (!(flags & X2_MAM_ROOM_MAM)) {
		purple_debug_info(X2_MAM_DEBUG, "room %s joined: no MAM, legacy "
		                  "history\n", room);
		g_free(room);
		return;
	}

	a = g_hash_table_lookup(st->rooms, room);
	if (a == NULL) {
		a = x2_mam_archive_new(st, room, TRUE);
		g_hash_table_insert(st->rooms, g_strdup(room), a);
	}
	a->extended = (flags & X2_MAM_ROOM_EXTENDED) != 0;
	x2_mam_sync_begin(a);
	x2_mam_muc_first_query(a);
	g_free(room);
}

X2MamArchive *
x2_mam_muc_archive(X2MamState *st, const char *jid)
{
	return jid ? g_hash_table_lookup(st->rooms, jid) : NULL;
}

void
x2_mam_muc_account_options(GList **opts)
{
	*opts = g_list_append(*opts, purple_account_option_int_new(
		_("Days a room's history position stays valid "
		  "(older: newest messages only)"),
		"mam_muc_threshold_days", X2_MAM_ROOM_THRESHOLD_DAYS));
}

void
x2_mam_muc_stream_new(X2MamState *st)
{
	st->rooms = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
	                                  (GDestroyNotify)x2_mam_archive_free);
	st->room_info = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
	                                      NULL);
	st->held_joins = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
	                                       held_join_free);
}

void
x2_mam_muc_stream_free(X2MamState *st)
{
	g_hash_table_destroy(st->held_joins);
	g_hash_table_destroy(st->rooms);
	g_hash_table_destroy(st->room_info);
}
