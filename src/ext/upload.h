/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file upload.h XEP-0363 HTTP File Upload (libpurple glue)
 *
 * upload.c: registry entry, discovery, the prpl file-transfer entry points.
 * upload_xfer.c: the PurpleXfer that requests a slot, PUTs the file and
 * announces the URL.  The pure protocol logic is in core/upload_proto.c.
 */
#ifndef XMPP2_EXT_UPLOAD_H_
#define XMPP2_EXT_UPLOAD_H_

#include <glib.h>
#include "connection.h"
#include "ft.h"

#include "ext/ext.h"

/** Registry entry, listed in the table in ext.c. */
extern const X2Ext x2_ext_upload;

/** Account option: allow http:// (unencrypted) upload and download URLs. */
#define X2_UPLOAD_OPT_PLAIN_HTTP "allow_plain_http_upload"

/** Per-stream state of the module (priv slot). */
typedef struct {
	char *service;      /* JID of our server's upload service, or NULL */
	guint64 max_size;   /* max-file-size announced by it, 0 = none */
	GList *xfers;       /* PurpleXfer *, the uploads in flight */
} X2UploadState;

X2UploadState *x2_upload_state(JabberStream *js);

/** TRUE if our server announced an upload service for this stream. */
gboolean x2_upload_available(JabberStream *js);

/**
 * Creates the upload transfer for @a who (a contact or a joined room).
 * The caller starts it with purple_xfer_request[_accepted]().
 */
PurpleXfer *x2_upload_xfer_new(JabberStream *js, const char *who,
                               gboolean groupchat);

/** Cancels every upload in flight (stream is going away). */
void x2_upload_xfer_cancel_all(JabberStream *js);

/*
 * prpl_info entry points.  Each falls back to the SI file transfer of the
 * inherited code when the server has no upload service.
 */
gboolean x2_upload_prpl_can_receive_file(PurpleConnection *gc, const char *who);
void x2_upload_prpl_send_file(PurpleConnection *gc, const char *who,
                              const char *file);
PurpleXfer *x2_upload_prpl_new_xfer(PurpleConnection *gc, const char *who);
gboolean x2_upload_prpl_chat_can_receive_file(PurpleConnection *gc, int id);
void x2_upload_prpl_chat_send_file(PurpleConnection *gc, int id,
                                   const char *file);

#endif /* XMPP2_EXT_UPLOAD_H_ */
