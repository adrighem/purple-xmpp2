/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file upload_http.c XEP-0363 streaming PUT (pure logic), see upload_http.h
 */
#include "upload_http.h"

#include <errno.h>
#include <string.h>

#include "upload_proto.h"

/* Interim responses tolerated before the final one */
#define MAX_INTERIM 4

gboolean
x2_upload_io_retry(int err)
{
	return err == EAGAIN || err == EWOULDBLOCK || err == EINTR;
}

X2UploadStatusClass
x2_upload_status_class(int code)
{
	if (x2_upload_put_status_ok(code))
		return X2_UPLOAD_STATUS_OK;
	if (code == 401 || code == 403)
		return X2_UPLOAD_STATUS_REFUSED;
	if (code == 413)
		return X2_UPLOAD_STATUS_TOO_LARGE;
	if (code >= 300 && code <= 399)
		return X2_UPLOAD_STATUS_REDIRECT;
	return X2_UPLOAD_STATUS_OTHER;
}

void
x2_upload_send_init(X2UploadSend *s, gsize head_len, guint64 body_size)
{
	g_return_if_fail(s != NULL);
	memset(s, 0, sizeof(*s));
	s->head_len = head_len;
	s->body_size = body_size;
}

gsize
x2_upload_send_want(const X2UploadSend *s, gsize cap)
{
	guint64 left = s->body_size - s->body_read;

	return left < cap ? (gsize)left : cap;
}

void
x2_upload_send_did_read(X2UploadSend *s, gsize n)
{
	g_return_if_fail(n <= s->body_size - s->body_read);
	s->body_read += n;
}

void
x2_upload_send_did_write(X2UploadSend *s, gsize n)
{
	/* never more than was handed out: head plus what was read */
	g_return_if_fail(n <= s->head_len + s->body_read - s->stream_sent);
	s->stream_sent += n;
}

guint64
x2_upload_send_body_sent(const X2UploadSend *s)
{
	return s->stream_sent > s->head_len ? s->stream_sent - s->head_len : 0;
}

gboolean
x2_upload_send_done(const X2UploadSend *s)
{
	return s->stream_sent == s->head_len + s->body_size;
}

gboolean
x2_upload_send_progress_due(X2UploadSend *s)
{
	guint64 sent = x2_upload_send_body_sent(s);
	guint64 step = s->body_size / 100;

	if (step < X2_UPLOAD_PROGRESS_MIN_STEP)
		step = X2_UPLOAD_PROGRESS_MIN_STEP;
	if (sent == s->reported)
		return FALSE;
	if (sent - s->reported < step && sent != s->body_size)
		return FALSE;
	s->reported = sent;
	return TRUE;
}

void
x2_upload_resp_init(X2UploadResp *r)
{
	g_return_if_fail(r != NULL);
	memset(r, 0, sizeof(*r));
}

/* Looks at the buffered bytes; drops an interim response and retries. */
static X2UploadRespState
resp_parse(X2UploadResp *r)
{
	for (;;) {
		char *end;
		gsize used;

		if (memchr(r->buf, '\n', r->len) == NULL)
			return r->len >= X2_UPLOAD_RESP_MAX ? X2_UPLOAD_RESP_BAD :
				X2_UPLOAD_RESP_MORE;
		if (!x2_upload_status_parse(r->buf, &r->code))
			return X2_UPLOAD_RESP_BAD;
		if (r->code >= 200)
			return X2_UPLOAD_RESP_DONE;
		if (r->code < 100 || ++r->interim > MAX_INTERIM)
			return X2_UPLOAD_RESP_BAD;

		/* 1xx: skip it up to its blank line, whatever follows is next */
		end = g_strstr_len(r->buf, r->len, "\r\n\r\n");
		if (end == NULL)
			return r->len >= X2_UPLOAD_RESP_MAX ? X2_UPLOAD_RESP_BAD :
				X2_UPLOAD_RESP_MORE;
		used = (gsize)(end - r->buf) + 4;
		memmove(r->buf, r->buf + used, r->len - used);
		r->len -= used;
		r->buf[r->len] = '\0';
	}
}

X2UploadRespState
x2_upload_resp_feed(X2UploadResp *r, const char *data, gsize len)
{
	gsize room;

	g_return_val_if_fail(r != NULL, X2_UPLOAD_RESP_BAD);
	room = X2_UPLOAD_RESP_MAX - r->len;
	if (len > room)
		len = room;
	if (len > 0) {
		memcpy(r->buf + r->len, data, len);
		r->len += len;
		r->buf[r->len] = '\0';
	}
	return resp_parse(r);
}
