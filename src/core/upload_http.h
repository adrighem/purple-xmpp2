/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file upload_http.h XEP-0363 streaming PUT (pure logic)
 *
 * The minimal HTTP/1.1 client of ext/upload_put.c is split in two parts
 * kept here, with no I/O: the accounting of what has been read from the
 * file and written to the socket (so memory use is one chunk, and progress
 * is reported in sensible steps), and the incremental parser of the
 * response status line.  Only <glib.h> is used.
 */
#ifndef XMPP2_CORE_UPLOAD_HTTP_H_
#define XMPP2_CORE_UPLOAD_HTTP_H_

#include <glib.h>

/** Size of the buffer between the file and the socket. */
#define X2_UPLOAD_CHUNK 65536

/** Largest part of a response we keep while looking for the status line. */
#define X2_UPLOAD_RESP_MAX 16384

/** Smallest step between two progress reports, in body bytes. */
#define X2_UPLOAD_PROGRESS_MIN_STEP 65536

/** Answers of the final status class, see x2_upload_status_class(). */
typedef enum {
	X2_UPLOAD_STATUS_OK,         /* 200, 201, 204 */
	X2_UPLOAD_STATUS_REFUSED,    /* 401, 403 */
	X2_UPLOAD_STATUS_TOO_LARGE,  /* 413 */
	X2_UPLOAD_STATUS_REDIRECT,   /* 3xx: never followed */
	X2_UPLOAD_STATUS_OTHER
} X2UploadStatusClass;

/**
 * What to tell the user about the status of the PUT.  A redirect is not
 * followed (it could lead to plain http or to another host, with the
 * slot headers).
 */
X2UploadStatusClass x2_upload_status_class(int code);

/**
 * Accounting of one request: @a head_len bytes of request head followed by
 * @a body_size bytes of body.  The caller reads the body in chunks and writes
 * head and chunks to the socket; the counters say what is left.
 */
typedef struct {
	gsize head_len;
	guint64 body_size;
	guint64 body_read;    /* body bytes taken from the file */
	guint64 stream_sent;  /* bytes (head and body) the socket accepted */
	guint64 reported;     /* body bytes sent at the last progress report */
} X2UploadSend;

void x2_upload_send_init(X2UploadSend *s, gsize head_len, guint64 body_size);

/** Body bytes to read next into a buffer of @a cap bytes (0 = all read). */
gsize x2_upload_send_want(const X2UploadSend *s, gsize cap);

/** @a n body bytes were read from the file. */
void x2_upload_send_did_read(X2UploadSend *s, gsize n);

/** The socket accepted @a n more bytes of the stream. */
void x2_upload_send_did_write(X2UploadSend *s, gsize n);

/** Body bytes the socket has accepted so far, never the head. */
guint64 x2_upload_send_body_sent(const X2UploadSend *s);

/** Has the whole request, head and body, been written? */
gboolean x2_upload_send_done(const X2UploadSend *s);

/**
 * Is a progress report due?  Yes after a step of one percent of the body
 * (at least ::X2_UPLOAD_PROGRESS_MIN_STEP bytes) and once at the end.  Call
 * it after each write; a TRUE answer is remembered.
 */
gboolean x2_upload_send_progress_due(X2UploadSend *s);

/**
 * Is @a err (errno of a failed read or write) "nothing happened, wait for
 * the next socket event" rather than a broken connection?  EAGAIN and
 * EWOULDBLOCK, and EINTR (a signal interrupted the call).
 */
gboolean x2_upload_io_retry(int err);

typedef enum {
	X2_UPLOAD_RESP_MORE,  /* feed more data */
	X2_UPLOAD_RESP_DONE,  /* ::code holds the final status */
	X2_UPLOAD_RESP_BAD    /* not HTTP, or the head is too long */
} X2UploadRespState;

/** Incremental reader of the status line of the (final) response. */
typedef struct {
	char buf[X2_UPLOAD_RESP_MAX + 1];
	gsize len;
	guint interim;  /* 1xx responses skipped */
	int code;
} X2UploadResp;

void x2_upload_resp_init(X2UploadResp *r);

/**
 * Feeds received bytes.  Interim 1xx responses (100 Continue, 102
 * Processing) are skipped up to a few; the body of the final response is
 * never needed and is not kept.
 */
X2UploadRespState x2_upload_resp_feed(X2UploadResp *r, const char *data,
                                      gsize len);

#endif /* XMPP2_CORE_UPLOAD_HTTP_H_ */
