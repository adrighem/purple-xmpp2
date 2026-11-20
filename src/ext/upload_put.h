/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file upload_put.h XEP-0363 HTTP File Upload: the streaming PUT client
 *
 * A minimal HTTP/1.1 client that sends one PUT with the file read from disk
 * in chunks over purple_proxy_connect() (http) or purple_ssl_connect()
 * (https, libpurple's certificate verification, the URL host is the SNI and
 * verification name).  Writes are non-blocking and driven by
 * purple_input_add(), so a slow server slows the file read, not the UI, and
 * memory use is one chunk.  The accounting and the response parser are in
 * core/upload_http.c.
 */
#ifndef XMPP2_EXT_UPLOAD_PUT_H_
#define XMPP2_EXT_UPLOAD_PUT_H_

#include <glib.h>
#include "account.h"

#include "core/upload_proto.h"

typedef struct X2UploadPut X2UploadPut;

/**
 * Called when the connection is up (@a sent is 0, the transfer starts) and
 * then as body bytes are written, in steps of about one percent.  It may
 * cancel the PUT with x2_upload_put_cancel().
 */
typedef void (*X2UploadPutProgress)(gpointer data, guint64 sent);

/**
 * Called once when the PUT ends.  @a status is the HTTP status of the final
 * response (any value), or 0 with @a error set (user language, no URL) when
 * there was none.  The X2UploadPut is gone before this is called.
 */
typedef void (*X2UploadPutDone)(gpointer data, int status, const char *error);

/**
 * Starts the PUT of the first @a size bytes of file @a path to the slot's
 * PUT URL.  The slot must have passed x2_upload_slot_parse().  Callbacks
 * are never called before this returns.
 *
 * @return The PUT in flight, or NULL with @a error set (no callback follows).
 */
X2UploadPut *x2_upload_put_start(PurpleAccount *account,
                                 const X2UploadSlot *slot, const char *path,
                                 guint64 size, const char *mime,
                                 X2UploadPutProgress progress,
                                 X2UploadPutDone done, gpointer data,
                                 GError **error);

/** Aborts the PUT and closes the connection, no callback follows. */
void x2_upload_put_cancel(X2UploadPut *put);

#endif /* XMPP2_EXT_UPLOAD_PUT_H_ */
