/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file upload_proto.h XEP-0363 HTTP File Upload (pure logic)
 *
 * Builds the slot request, parses and vets the slot, builds the HTTP PUT
 * request head and the XEP-0066 message that carries the download URL.
 * Only <glib.h> and libpurple's xmlnode.h are used, so this file is unit
 * tested without a running libpurple core.
 */
#ifndef XMPP2_CORE_UPLOAD_PROTO_H_
#define XMPP2_CORE_UPLOAD_PROTO_H_

#include <glib.h>
#include "xmlnode.h"

#define X2_NS_HTTP_UPLOAD "urn:xmpp:http:upload:0"
#define X2_NS_OOB_X       "jabber:x:oob"

/** Hard cap on the size of one upload: v1 holds the file in memory. */
#define X2_UPLOAD_HARD_CAP ((guint64)100 * 1024 * 1024)

#define X2_UPLOAD_ERROR x2_upload_error_quark()
GQuark x2_upload_error_quark(void);

typedef enum {
	X2_UPLOAD_ERROR_URL,     /* a URL in the slot is unusable */
	X2_UPLOAD_ERROR_HEADER,  /* a slot header would inject into the request */
	X2_UPLOAD_ERROR_SLOT,    /* the slot is malformed */
	X2_UPLOAD_ERROR_SIZE     /* the file is empty or too large */
} X2UploadError;

/** A parsed http(s) URL: the parts an HTTP request needs. */
typedef struct {
	gboolean https;
	char *host;
	int port;           /* explicit or the scheme default */
	char *path;         /* path and query with leading '/', no fragment */
} X2UploadUrl;

/** One request header taken from the slot (name is canonical). */
typedef struct {
	char *name;
	char *value;
} X2UploadHeader;

typedef struct {
	char *put;          /* PUT URL as offered, trimmed */
	char *get;          /* GET URL as offered, trimmed */
	X2UploadUrl put_url;
	GPtrArray *headers; /* X2UploadHeader *, only the allowed ones */
	guint dropped;      /* headers ignored because not allowed */
} X2UploadSlot;

/**
 * Reads a disco#info result of a service.
 *
 * @param query     The <query xmlns='.../disco#info'/> element.
 * @param max_size  Receives max-file-size in bytes, 0 when not announced.
 * @return TRUE if the service advertises urn:xmpp:http:upload:0.
 */
gboolean x2_upload_disco_parse(const xmlnode *query, guint64 *max_size);

/**
 * Checks a file size against the service limit (0 = none) and the hard cap.
 * The error message is meant for the user.
 */
gboolean x2_upload_check_size(guint64 size, guint64 service_max, GError **error);

/** Returns the limit that applies: the smaller of service_max (0 = none) and the cap. */
guint64 x2_upload_effective_max(guint64 service_max);

/** Base name of a path, valid UTF-8, no control characters. Free with g_free(). */
char *x2_upload_filename(const char *path);

/** MIME type guessed from the file name extension, never NULL. */
const char *x2_upload_guess_mime(const char *filename);

/** Builds <request xmlns='urn:xmpp:http:upload:0' filename size content-type/>. */
xmlnode *x2_upload_request_new(const char *filename, guint64 size,
                               const char *mime);

/**
 * Parses a URL into @a out.  Only http and https, no user info, no IPv6
 * literal, no control characters or spaces.  Free with x2_upload_url_clear().
 */
gboolean x2_upload_url_parse(const char *url, X2UploadUrl *out, GError **error);
void x2_upload_url_clear(X2UploadUrl *url);

/**
 * Parses and vets a <slot/>.  Only Authorization, Cookie and Expires are
 * kept (names are matched case-insensitively, others are counted in
 * ::dropped); a kept value with a control character fails the whole slot
 * (header injection).  Both URLs must be https unless @a allow_http.
 *
 * @return A new slot, or NULL with @a error set.  Free with x2_upload_slot_free().
 */
X2UploadSlot *x2_upload_slot_parse(const xmlnode *slot, gboolean allow_http,
                                   GError **error);
void x2_upload_slot_free(X2UploadSlot *slot);

/**
 * Builds the head of the PUT request (request line, headers, blank line).
 * The caller appends exactly @a size body bytes.
 *
 * @return A newly allocated string ending in CRLF CRLF.
 */
char *x2_upload_put_head(const X2UploadSlot *slot, guint64 size,
                         const char *mime, const char *user_agent);

/**
 * Reads the status code of an HTTP response (headers included).
 *
 * @return TRUE and the code, FALSE if there is no valid status line.
 */
gboolean x2_upload_status_parse(const char *response, int *code);

/**
 * Builds the message that announces an upload: body is the URL (clients
 * without XEP-0066 show it) plus <x xmlns='jabber:x:oob'><url/></x>.
 */
xmlnode *x2_upload_message_new(const char *to, gboolean groupchat,
                               const char *url);

#endif /* XMPP2_CORE_UPLOAD_PROTO_H_ */
