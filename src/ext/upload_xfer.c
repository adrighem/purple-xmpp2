/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file upload_xfer.c XEP-0363 HTTP File Upload: the PurpleXfer
 *
 * Flow: init (size check, no network) -> slot request IQ to our server's
 * upload service -> HTTP PUT with purple_util_fetch_url_request_data_len_
 * with_account() (honours the account proxy, https through libpurple's SSL
 * plugin) -> message with the GET URL and an XEP-0066 <x/> -> xfer complete.
 *
 * v1 reads the whole file into memory, bounded by max-file-size and the
 * 100 MiB hard cap (ARCHITECTURE 7.7).  Neither the slot URLs (they may
 * carry a signed token) nor the slot headers (Authorization, Cookie) are
 * ever logged here; libpurple itself logs the host and path of a fetch at
 * debug level "util".
 */
#include "xmpp2_internal.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <glib/gstdio.h>

#include "conversation.h"
#include "debug.h"
#include "util.h"

#include "core/upload_proto.h"
#include "ext/ext.h"
#include "ext/upload.h"
#include "iq.h"
#include "jutil.h"
#include "namespaces.h"

#define DBG "xmpp2-upload"

/* Largest PUT response we read (status line, headers, short body) */
#define X2_UPLOAD_RESPONSE_MAX (64 * 1024)

typedef struct {
	JabberStream *js;
	PurpleXfer *xfer;
	gboolean groupchat;
	char *filename;
	const char *mime;
	char *iq_id;                    /* slot request in flight */
	X2UploadSlot *slot;
	PurpleUtilFetchUrlData *fetch;  /* PUT in flight */
	gboolean starting;              /* inside the fetch call, see put_done() */
	gboolean failed_early;
	char *early_error;
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
	if (ux->fetch != NULL)
		purple_util_fetch_url_cancel(ux->fetch);
	x2_upload_slot_free(ux->slot);
	g_free(ux->iq_id);
	g_free(ux->filename);
	g_free(ux->early_error);
	g_free(ux);
}

/* Tells the user why the upload failed, then cancels the transfer. */
static void G_GNUC_PRINTF(2, 3)
ux_fail(PurpleXfer *xfer, const char *fmt, ...)
{
	X2UploadXfer *ux = xfer->data;
	const char *name;
	char *reason, *msg;
	va_list ap;

	va_start(ap, fmt);
	reason = g_strdup_vprintf(fmt, ap);
	va_end(ap);
	name = ux->filename ? ux->filename : purple_xfer_get_filename(xfer);
	msg = g_strdup_printf(_("File upload of %s failed: %s"),
		name ? name : _("(unnamed)"), reason);
	purple_debug_warning(DBG, "%s\n", msg);
	purple_xfer_error(PURPLE_XFER_SEND, purple_xfer_get_account(xfer),
		purple_xfer_get_remote_user(xfer), msg);
	g_free(msg);
	g_free(reason);
	purple_xfer_cancel_local(xfer);
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

/* Handles the outcome of the PUT; error is NULL when a response arrived. */
static void
put_result(PurpleXfer *xfer, const char *response, const char *error)
{
	int code = 0;

	if (error != NULL) {
		ux_fail(xfer, _("could not send the file to the server: %s"), error);
		return;
	}
	if (!x2_upload_status_parse(response, &code)) {
		ux_fail(xfer, _("the upload server sent an unreadable response"));
		return;
	}
	/*
	 * Only 201 Created is success (XEP-0363).  libpurple follows any
	 * Location header with a GET, possibly over plain http, and that GET
	 * would answer 200 with somebody's content: a redirect is a failure.
	 */
	if (code != 201) {
		if (code == 401 || code == 403)
			ux_fail(xfer, _("the upload server refused the file (HTTP %d)"), code);
		else if (code == 413)
			ux_fail(xfer, _("the file is too large for the upload server (HTTP 413)"));
		else if (code >= 300 && code <= 399)
			ux_fail(xfer, _("the upload server redirected the upload "
				"(HTTP %d), which is not supported"), code);
		else
			ux_fail(xfer, _("the upload server answered HTTP %d"), code);
		return;
	}
	ux_finish(xfer);
}

static void
put_done(PurpleUtilFetchUrlData *url_data, gpointer data, const gchar *text,
         gsize len, const gchar *error_message)
{
	PurpleXfer *xfer = data;
	X2UploadXfer *ux = xfer->data;

	if (ux->starting) {
		/*
		 * libpurple reported a failure from inside the fetch call, before
		 * it returned: handle it once ux->fetch is settled.
		 */
		ux->failed_early = TRUE;
		ux->early_error = g_strdup(error_message ? error_message :
			"the connection failed");
		return;
	}
	ux->fetch = NULL;
	put_result(xfer, text, error_message);
}

/*
 * Reads head and file into one buffer (*out, *out_len): one copy of the file
 * in memory until the fetch call copies it.
 */
static gboolean
build_request(PurpleXfer *xfer, char **out, gsize *out_len, GError **error)
{
	X2UploadXfer *ux = xfer->data;
	guint64 size = purple_xfer_get_size(xfer);
	char *head = x2_upload_put_head(ux->slot, size, ux->mime,
		"purple-xmpp2/" XMPP2_VERSION);
	gsize hlen = strlen(head);
	FILE *fp;
	char *buf;

	buf = g_try_malloc(hlen + size);
	if (buf == NULL) {
		g_set_error_literal(error, X2_UPLOAD_ERROR, X2_UPLOAD_ERROR_SIZE,
			"not enough memory to read the file");
		g_free(head);
		return FALSE;
	}
	memcpy(buf, head, hlen);
	g_free(head);

	fp = g_fopen(purple_xfer_get_local_filename(xfer), "rb");
	if (fp == NULL) {
		g_set_error(error, X2_UPLOAD_ERROR, X2_UPLOAD_ERROR_SIZE,
			"cannot read the file: %s", g_strerror(errno));
		g_free(buf);
		return FALSE;
	}
	if (fread(buf + hlen, 1, size, fp) != size) {
		g_set_error_literal(error, X2_UPLOAD_ERROR, X2_UPLOAD_ERROR_SIZE,
			"the file changed while it was read");
		fclose(fp);
		g_free(buf);
		return FALSE;
	}
	fclose(fp);
	*out = buf;
	*out_len = hlen + size;
	return TRUE;
}

static void
start_put(PurpleXfer *xfer)
{
	X2UploadXfer *ux = xfer->data;
	GError *err = NULL;
	char *request;
	gsize request_len;

	if (!build_request(xfer, &request, &request_len, &err)) {
		ux_fail(xfer, "%s", err->message);
		g_clear_error(&err);
		return;
	}
	purple_debug_info(DBG, "PUT %" G_GUINT64_FORMAT " bytes to %s (%s)\n",
		(guint64)purple_xfer_get_size(xfer), ux->slot->put_url.host,
		ux->slot->put_url.https ? "https" : "http");

	/*
	 * max_len bounds the response.  It must be > 0: with -1 libpurple logs
	 * the full (signed) PUT URL at error level.  libpurple copies the
	 * request, so ours goes at once (halves peak memory).
	 */
	ux->starting = TRUE;
	ux->fetch = purple_util_fetch_url_request_data_len_with_account(
		purple_xfer_get_account(xfer), ux->slot->put, TRUE,
		"purple-xmpp2/" XMPP2_VERSION, TRUE, request, request_len,
		TRUE, X2_UPLOAD_RESPONSE_MAX, put_done, xfer);
	ux->starting = FALSE;
	g_free(request);

	if (ux->failed_early) {
		ux->fetch = NULL;
		put_result(xfer, NULL, ux->early_error);
	} else if (ux->fetch == NULL) {
		ux_fail(xfer, _("could not start the upload connection"));
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

		ux_fail(xfer, _("the server did not give an upload slot: %s"), why);
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
		ux_fail(xfer, "%s", err->message);
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
		ux_fail(xfer, "%s", err->message);
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
