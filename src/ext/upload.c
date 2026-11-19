/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file upload.c XEP-0363 HTTP File Upload: discovery and prpl entry points
 *
 * The upload service is taken only from disco#info results of our own
 * server (the server_info hook never sees foreign entities, ext.h), so a
 * contact or a foreign domain can never become the place files are sent to.
 */
#include "xmpp2_internal.h"

#include "accountopt.h"
#include "debug.h"
#include "notify.h"

#include "core/upload_proto.h"
#include "ext/upload.h"
#include "chat.h"
#include "jutil.h"
#include "si.h"

#define DBG "xmpp2-upload"

static void
upload_account_options(GList **opts)
{
	*opts = g_list_append(*opts, purple_account_option_bool_new(
		_("Allow unencrypted file upload"), X2_UPLOAD_OPT_PLAIN_HTTP, FALSE));
}

static void
upload_stream_new(JabberStream *js)
{
	x2_ext_set_priv(js, &x2_ext_upload, g_new0(X2UploadState, 1));
}

static void
upload_stream_free(JabberStream *js)
{
	X2UploadState *st = x2_upload_state(js);

	if (st == NULL)
		return;
	x2_upload_xfer_cancel_all(js);
	g_free(st->service);
	g_free(st);
	x2_ext_set_priv(js, &x2_ext_upload, NULL);
}

static void
upload_server_info(JabberStream *js, const char *from, xmlnode *query)
{
	X2UploadState *st = x2_upload_state(js);
	char *bare = jabber_id_get_bare_jid(js->user);
	guint64 max;

	/* our own account is never an upload service */
	if (st == NULL || purple_strequal(from, bare)) {
		g_free(bare);
		return;
	}
	g_free(bare);
	if (!x2_upload_disco_parse(query, &max))
		return;
	if (st->service != NULL && !purple_strequal(st->service, from)) {
		purple_debug_info(DBG, "ignoring second upload service %s\n", from);
		return;
	}
	if (st->service == NULL)
		st->service = g_strdup(from);
	st->max_size = max;
	purple_debug_info(DBG, "upload service %s, max-file-size %" G_GUINT64_FORMAT
		"\n", from, max);
}

const X2Ext x2_ext_upload = {
	.name = "upload",
	.account_options = upload_account_options,
	.stream_new = upload_stream_new,
	.stream_free = upload_stream_free,
	.server_info = upload_server_info,
};

X2UploadState *
x2_upload_state(JabberStream *js)
{
	return js ? x2_ext_get_priv(js, &x2_ext_upload) : NULL;
}

gboolean
x2_upload_available(JabberStream *js)
{
	X2UploadState *st = x2_upload_state(js);

	return st != NULL && st->service != NULL;
}

/* TRUE if @a who is the bare JID of a room we have joined */
static gboolean
is_joined_room(JabberStream *js, const char *who)
{
	JabberID *jid = jabber_id_new(who);
	gboolean room;

	if (jid == NULL)
		return FALSE;
	room = jid->node != NULL && jid->resource == NULL &&
	       jabber_chat_find(js, jid->node, jid->domain) != NULL;
	jabber_id_free(jid);
	return room;
}

gboolean
x2_upload_prpl_can_receive_file(PurpleConnection *gc, const char *who)
{
	/* the URL reaches anybody, online or not, whatever their client */
	if (x2_upload_available(gc->proto_data))
		return TRUE;
	return jabber_can_receive_file(gc, who);
}

PurpleXfer *
x2_upload_prpl_new_xfer(PurpleConnection *gc, const char *who)
{
	JabberStream *js = gc->proto_data;

	if (!x2_upload_available(js))
		return jabber_si_new_xfer(gc, who);
	return x2_upload_xfer_new(js, who, is_joined_room(js, who));
}

void
x2_upload_prpl_send_file(PurpleConnection *gc, const char *who,
                         const char *file)
{
	PurpleXfer *xfer;

	if (!x2_upload_available(gc->proto_data)) {
		jabber_si_xfer_send(gc, who, file);
		return;
	}
	xfer = x2_upload_prpl_new_xfer(gc, who);
	if (xfer == NULL)
		return;
	if (file != NULL)
		purple_xfer_request_accepted(xfer, file);
	else
		purple_xfer_request(xfer);
}

gboolean
x2_upload_prpl_chat_can_receive_file(PurpleConnection *gc, int id)
{
	JabberStream *js = gc->proto_data;

	return x2_upload_available(js) && jabber_chat_find_by_id(js, id) != NULL;
}

void
x2_upload_prpl_chat_send_file(PurpleConnection *gc, int id, const char *file)
{
	JabberStream *js = gc->proto_data;
	JabberChat *chat = jabber_chat_find_by_id(js, id);
	PurpleXfer *xfer;
	char *room;

	if (chat == NULL)
		return;
	room = g_strdup_printf("%s@%s", chat->room, chat->server);
	if (!x2_upload_available(js)) {
		purple_notify_error(gc, _("File upload"),
			_("Cannot send the file to the chat room"),
			_("The server offers no HTTP file upload service."));
		g_free(room);
		return;
	}
	xfer = x2_upload_xfer_new(js, room, TRUE);
	g_free(room);
	if (file != NULL)
		purple_xfer_request_accepted(xfer, file);
	else
		purple_xfer_request(xfer);
}
