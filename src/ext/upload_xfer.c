/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file upload_xfer.c XEP-0363 HTTP File Upload: the PurpleXfer
 *
 * Flow: init (size check, no network) -> slot request IQ to our server's
 * upload service -> HTTP PUT streamed from disk by upload_put.c (account
 * proxy, https through libpurple's SSL plugin, progress on the PurpleXfer)
 * -> message with the GET URL and an XEP-0066 <x/> -> xfer complete.
 *
 * Memory use is one chunk whatever the file size; the only size limit is
 * the server's max-file-size (ARCHITECTURE 7.7).  Neither the slot URLs
 * (they may carry a signed token) nor the slot headers (Authorization,
 * Cookie) are ever logged here.
 */
#include "xmpp2_internal.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <glib/gstdio.h>

#include "conversation.h"
#include "debug.h"

#include "core/upload_http.h"
#include "core/upload_proto.h"
#include "ext/ext.h"
#include "ext/upload.h"
#include "ext/upload_put.h"
#include "iq.h"
#include "jutil.h"
#include "namespaces.h"

#define DBG "xmpp2-upload"

typedef struct {
	JabberStream *js;
	PurpleXfer *xfer;
	gboolean groupchat;
	char *filename;
	const char *mime;
	char *iq_id;                    /* slot request in flight */
	X2UploadSlot *slot;
	X2UploadPut *put;               /* PUT in flight */
} X2UploadXfer;

static void
ux_free(PurpleXfer *xfer)
{
	X2UploadXfer *ux = xfer->data;
	X2UploadState *st;

	if (ux == NULL)
		return;
	xfer->data = NULL;
	st = x2_upload_state(ux->js);
	if (st != NULL)
		st->xfers = g_list_remove(st->xfers, xfer);
	if (ux->iq_id != NULL)
		jabber_iq_remove_callback_by_id(ux->js, ux->iq_id);
	x2_upload_put_cancel(ux->put);
	x2_upload_slot_free(ux->slot);
	g_free(ux->iq_id);
	g_free(ux->filename);
	g_free(ux);
}

/* Tells the user why the upload failed, then cancels the transfer. */
static void
ux_fail(PurpleXfer *xfer, const char *reason)
{
	X2UploadXfer *ux = xfer->data;
	const char *name;
	char *msg;

	name = ux->filename ? ux->filename : purple_xfer_get_filename(xfer);
	msg = g_strdup_printf(_("File upload of %s failed: %s"),
		name ? name : _("(unnamed)"), reason);
	purple_debug_warning(DBG, "%s\n", msg);
	purple_xfer_error(PURPLE_XFER_SEND, purple_xfer_get_account(xfer),
		purple_xfer_get_remote_user(xfer), msg);
	g_free(msg);
	purple_xfer_cancel_local(xfer);
}

/* ux_fail() with a printf-style reason */
static void G_GNUC_PRINTF(2, 3)
ux_failf(PurpleXfer *xfer, const char *fmt, ...)
{
	char *reason;
	va_list ap;

	va_start(ap, fmt);
	reason = g_strdup_vprintf(fmt, ap);
	va_end(ap);
	ux_fail(xfer, reason);
	g_free(reason);
}

static void
ux_finish(PurpleXfer *xfer)
{
	X2UploadXfer *ux = xfer->data;
	JabberStream *js = ux->js;
	const char *who = purple_xfer_get_remote_user(xfer);
	char *url = g_strdup(ux->slot->get);
	xmlnode *message;

	message = x2_upload_message_new(who, ux->groupchat, url);
	x2_msg_prepare_outgoing(js, message);
	jabber_send(js, message);
	xmlnode_free(message);

	/* A room reflects the message back; a 1:1 conversation needs the echo */
	if (!ux->groupchat) {
		PurpleAccount *account = purple_xfer_get_account(xfer);
		PurpleConversation *conv = purple_find_conversation_with_account(
			PURPLE_CONV_TYPE_IM, who, account);

		if (conv == NULL)
			conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, who);
		purple_conv_im_write(PURPLE_CONV_IM(conv), NULL, url,
			PURPLE_MESSAGE_SEND, time(NULL));
	}
	g_free(url);

	purple_debug_info(DBG, "upload done, announced to %s\n", who);
	purple_xfer_set_bytes_sent(xfer, purple_xfer_get_size(xfer));
	purple_xfer_update_progress(xfer);
	purple_xfer_set_completed(xfer, TRUE);
	purple_xfer_end(xfer);
}

/* Handles the outcome of the PUT: a status, or 0 with the error. */
static void
put_result(PurpleXfer *xfer, int code, const char *error)
{
	if (code == 0) {
		ux_failf(xfer, _("could not send the file to the server: %s"), error);
		return;
	}
	/*
	 * 200, 201 and 204 are success (XEP-0363 says 201, servers differ).  A
	 * redirect is never followed (it could lead to plain http, or to
	 * another host with our slot headers): it fails with its own message.
	 */
	switch (x2_upload_status_class(code)) {
	case X2_UPLOAD_STATUS_OK:
		ux_finish(xfer);
		break;
	case X2_UPLOAD_STATUS_REFUSED:
		ux_failf(xfer, _("the upload server refused the file (HTTP %d)"), code);
		break;
	case X2_UPLOAD_STATUS_TOO_LARGE:
		ux_fail(xfer, _("the file is too large for the upload server (HTTP 413)"));
		break;
	case X2_UPLOAD_STATUS_REDIRECT:
		ux_failf(xfer, _("the upload server redirected the upload "
			"(HTTP %d), which is not supported"), code);
		break;
	default:
		ux_failf(xfer, _("the upload server answered HTTP %d"), code);
		break;
	}
}

static void
put_done(gpointer data, int status, const char *error)
{
	PurpleXfer *xfer = data;
	X2UploadXfer *ux = xfer->data;

	ux->put = NULL;  /* gone: it freed itself before calling us */
	put_result(xfer, status, error);
}

/*
 * The PUT is connected (sent == 0) or has written more of the body: move
 * the PurpleXfer along, so the transfer dialog shows the progress.
 */
static void
put_progress(gpointer data, guint64 sent)
{
	PurpleXfer *xfer = data;

	if (purple_xfer_get_status(xfer) != PURPLE_XFER_STATUS_STARTED) {
		gboolean gone;

		/*
		 * No descriptor of ours for libpurple (-1): this only marks the
		 * transfer as started (start time, status).  It cancels the xfer
		 * if it cannot open the file, hence the extra reference.
		 */
		purple_xfer_ref(xfer);
		purple_xfer_start(xfer, -1, NULL, 0);
		gone = purple_xfer_is_canceled(xfer);
		purple_xfer_unref(xfer);
		if (gone)
			return;
	}
	purple_xfer_set_bytes_sent(xfer, sent);
	purple_xfer_update_progress(xfer);
}

static void
start_put(PurpleXfer *xfer)
{
	X2UploadXfer *ux = xfer->data;
	GError *err = NULL;

	purple_debug_info(DBG, "PUT %" G_GUINT64_FORMAT " bytes to %s (%s)\n",
		(guint64)purple_xfer_get_size(xfer), ux->slot->put_url.host,
		ux->slot->put_url.https ? "https" : "http");
	ux->put = x2_upload_put_start(purple_xfer_get_account(xfer), ux->slot,
		purple_xfer_get_local_filename(xfer), purple_xfer_get_size(xfer),
		ux->mime, put_progress, put_done, xfer, &err);
	if (ux->put == NULL) {
		ux_fail(xfer, err->message);
		g_clear_error(&err);
	}
}

static char *
slot_error_text(JabberStream *js, xmlnode *packet)
{
	xmlnode *error = xmlnode_get_child(packet, "error");
	xmlnode *too_large = error ?
		xmlnode_get_child_with_namespace(error, "file-too-large",
			X2_NS_HTTP_UPLOAD) : NULL;
	char *msg;

	if (too_large != NULL)
		return g_strdup(_("the server says the file is too large"));
	msg = jabber_parse_error(js, packet, NULL);
	return msg ? msg : g_strdup(_("the server refused the request"));
}

static void
slot_cb(JabberStream *js, const char *from, JabberIqType type, const char *id,
        xmlnode *packet, gpointer data)
{
	PurpleXfer *xfer = data;
	X2UploadXfer *ux = xfer->data;
	X2UploadState *st = x2_upload_state(js);
	xmlnode *slot;
	GError *err = NULL;

	g_free(ux->iq_id);
	ux->iq_id = NULL;

	if (type == JABBER_IQ_ERROR) {
		char *why = slot_error_text(js, packet);

		ux_failf(xfer, _("the server did not give an upload slot: %s"), why);
		g_free(why);
		return;
	}
	/* Only the service we discovered may hand out a slot */
	if (st == NULL || st->service == NULL ||
	    !purple_strequal(from, st->service)) {
		ux_fail(xfer, _("the upload slot came from an unexpected sender"));
		return;
	}
	slot = xmlnode_get_child_with_namespace(packet, "slot", X2_NS_HTTP_UPLOAD);
	if (slot == NULL) {
		ux_fail(xfer, _("the server sent no upload slot"));
		return;
	}
	ux->slot = x2_upload_slot_parse(slot,
		purple_account_get_bool(purple_xfer_get_account(xfer),
			X2_UPLOAD_OPT_PLAIN_HTTP, FALSE), &err);
	if (ux->slot == NULL) {
		ux_fail(xfer, err->message);
		g_clear_error(&err);
		return;
	}
	if (ux->slot->dropped > 0)
		purple_debug_info(DBG, "ignored %u unsupported slot header(s)\n",
			ux->slot->dropped);
	start_put(xfer);
}

static void
xfer_init(PurpleXfer *xfer)
{
	X2UploadXfer *ux = xfer->data;
	X2UploadState *st = x2_upload_state(ux->js);
	guint64 size = purple_xfer_get_size(xfer);
	GError *err = NULL;
	xmlnode *request;
	JabberIq *iq;

	ux->filename = x2_upload_filename(purple_xfer_get_local_filename(xfer));
	purple_xfer_set_filename(xfer, ux->filename);
	ux->mime = x2_upload_guess_mime(ux->filename);

	if (st == NULL || st->service == NULL) {
		ux_fail(xfer, _("the server has no upload service"));
		return;
	}
	/* Before any network request: the user gets the limit, not a 413 */
	if (!x2_upload_check_size(size, st->max_size, &err)) {
		ux_fail(xfer, err->message);
		g_clear_error(&err);
		return;
	}

	iq = jabber_iq_new(ux->js, JABBER_IQ_GET);
	xmlnode_set_attrib(iq->node, "to", st->service);
	request = x2_upload_request_new(ux->filename, size, ux->mime);
	xmlnode_insert_child(iq->node, request);
	jabber_iq_set_callback(iq, slot_cb, xfer);
	ux->iq_id = g_strdup(iq->id);
	jabber_iq_send(iq);
	purple_debug_info(DBG, "requested an upload slot from %s\n", st->service);
}

PurpleXfer *
x2_upload_xfer_new(JabberStream *js, const char *who, gboolean groupchat)
{
	X2UploadState *st = x2_upload_state(js);
	X2UploadXfer *ux;
	PurpleXfer *xfer;

	g_return_val_if_fail(st != NULL, NULL);
	xfer = purple_xfer_new(purple_connection_get_account(js->gc),
		PURPLE_XFER_SEND, who);
	if (xfer == NULL)
		return NULL;
	ux = g_new0(X2UploadXfer, 1);
	ux->js = js;
	ux->xfer = xfer;
	ux->groupchat = groupchat;
	xfer->data = ux;
	purple_xfer_set_init_fnc(xfer, xfer_init);
	purple_xfer_set_request_denied_fnc(xfer, ux_free);
	purple_xfer_set_cancel_send_fnc(xfer, ux_free);
	purple_xfer_set_end_fnc(xfer, ux_free);
	st->xfers = g_list_prepend(st->xfers, xfer);
	return xfer;
}

void
x2_upload_xfer_cancel_all(JabberStream *js)
{
	X2UploadState *st = x2_upload_state(js);

	while (st != NULL && st->xfers != NULL) {
		PurpleXfer *xfer = st->xfers->data;

		purple_xfer_cancel_local(xfer);
		/* cancel_send frees the data and unlinks; make sure we progress */
		if (st->xfers != NULL && st->xfers->data == xfer)
			st->xfers = g_list_delete_link(st->xfers, st->xfers);
	}
}
