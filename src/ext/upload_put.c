/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file upload_put.c XEP-0363 HTTP File Upload: the streaming PUT client
 *
 * Connect (purple_proxy_connect or purple_ssl_connect), then one watcher on
 * the socket: READ | WRITE while the request is being written, so an early
 * answer (413, 401) is seen at once, READ only afterwards.  A write moves at
 * most X2_UPLOAD_BURST bytes per main loop turn: head first, then chunks
 * read from the file, one buffer in all.  On EAGAIN the same buffer is
 * offered again (GnuTLS insists on that).  Nothing here logs a URL or a
 * header.
 */
#include "xmpp2_internal.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "debug.h"
#include "eventloop.h"
#include "proxy.h"
#include "sslconn.h"

#include "core/upload_http.h"
#include "ext/upload_put.h"

#define DBG "xmpp2-upload"

/* Bytes written per main loop turn, so the UI stays responsive */
#define X2_UPLOAD_BURST (256 * 1024)

struct X2UploadPut {
	X2UploadPutProgress progress_cb;
	X2UploadPutDone done_cb;
	gpointer data;

	char *host;
	gboolean https;
	PurpleProxyConnectData *connect;  /* plain: connecting */
	PurpleSslConnection *ssl;         /* https: connecting or connected */
	int fd;                           /* -1 until connected */
	guint watcher;

	FILE *fp;
	char *buf;                        /* head, then body chunks */
	gsize buf_cap, buf_len, buf_off;  /* bytes buf[buf_off..buf_len) pending */
	X2UploadSend send;
	X2UploadResp resp;

	gboolean started;                 /* progress(0) was reported */
	gboolean write_failed;            /* the socket refused our data */
	int write_errno;

	gboolean starting;                /* inside the connect call */
	char *early_error;                /* failure reported while starting */
	gboolean busy;                    /* inside a callback to the caller */
	gboolean cancelled;               /* cancelled while busy */
};

static void
put_destroy(X2UploadPut *put)
{
	if (put->watcher != 0)
		purple_input_remove(put->watcher);
	if (put->ssl != NULL)
		purple_ssl_close(put->ssl);  /* closes the fd as well */
	else if (put->connect != NULL)
		purple_proxy_connect_cancel(put->connect);
	else if (put->fd >= 0)
		close(put->fd);
	if (put->fp != NULL)
		fclose(put->fp);
	g_free(put->host);
	g_free(put->buf);
	g_free(put->early_error);
	g_free(put);
}

void
x2_upload_put_cancel(X2UploadPut *put)
{
	if (put == NULL)
		return;
	if (put->busy) {
		/* the caller is inside one of our callbacks: free on return */
		put->cancelled = TRUE;
		return;
	}
	put_destroy(put);
}

/*
 * Ends the PUT: frees it, then tells the caller.  Everything of ours is
 * gone when this returns, so callers return at once (see the FALSE results).
 */
static void
put_finish(X2UploadPut *put, int status, const char *error)
{
	X2UploadPutDone done = put->done_cb;
	gpointer data = put->data;
	char *msg = g_strdup(error);

	if (put->starting) {
		/* not reported by a callback before start returned */
		g_free(put->early_error);
		put->early_error = msg;
		return;
	}
	put_destroy(put);
	done(data, status, msg);
	g_free(msg);
}

static void G_GNUC_PRINTF(2, 3)
put_failf(X2UploadPut *put, const char *fmt, ...)
{
	char *msg;
	va_list ap;

	va_start(ap, fmt);
	msg = g_strdup_vprintf(fmt, ap);
	va_end(ap);
	put_finish(put, 0, msg);
	g_free(msg);
}

static gssize
put_recv(X2UploadPut *put, void *buf, gsize len)
{
	if (put->https)
		return purple_ssl_read(put->ssl, buf, len);
	return recv(put->fd, buf, len, 0);
}

static gssize
put_send(X2UploadPut *put, const void *buf, gsize len)
{
	if (put->https)
		return purple_ssl_write(put->ssl, buf, len);
	return send(put->fd, buf, len, MSG_NOSIGNAL);
}

static void put_io_cb(gpointer data, gint source, PurpleInputCondition cond);

static void
put_watch(X2UploadPut *put, PurpleInputCondition cond)
{
	if (put->watcher != 0)
		purple_input_remove(put->watcher);
	put->watcher = purple_input_add(put->fd, cond, put_io_cb, put);
}

/*
 * Reads what the server has to say.  Returns FALSE when the PUT has ended
 * (and is gone), TRUE to keep waiting.
 */
static gboolean
put_read(X2UploadPut *put)
{
	char tmp[2048];

	for (;;) {
		gssize n = put_recv(put, tmp, sizeof(tmp));

		if (n < 0 && errno == EAGAIN)
			return TRUE;
		if (n <= 0) {
			if (put->write_failed)
				put_failf(put, _("the connection failed while sending the "
					"file: %s"), g_strerror(put->write_errno));
			else if (n < 0)
				put_failf(put, _("the connection failed: %s"),
					g_strerror(errno));
			else
				put_failf(put, _("the server closed the connection %s"),
					x2_upload_send_done(&put->send) ?
					_("without an answer") :
					_("before the upload was complete"));
			return FALSE;
		}
		switch (x2_upload_resp_feed(&put->resp, tmp, (gsize)n)) {
		case X2_UPLOAD_RESP_DONE:
			if (!x2_upload_send_done(&put->send) &&
			    x2_upload_status_class(put->resp.code) ==
			        X2_UPLOAD_STATUS_OK) {
				/* "stored" before the whole file was sent: it was not */
				put_failf(put, "%s", _("the upload server answered before "
					"the whole file was sent"));
				return FALSE;
			}
			put_finish(put, put->resp.code, NULL);
			return FALSE;
		case X2_UPLOAD_RESP_BAD:
			put_failf(put, "%s",
				_("the upload server sent an unreadable response"));
			return FALSE;
		case X2_UPLOAD_RESP_MORE:
			break;
		}
	}
}

/* Calls the progress callback; FALSE if that cancelled the PUT (now gone). */
static gboolean
put_report(X2UploadPut *put, guint64 sent)
{
	put->busy = TRUE;
	put->progress_cb(put->data, sent);
	put->busy = FALSE;
	if (put->cancelled) {
		put_destroy(put);
		return FALSE;
	}
	return TRUE;
}

/* Refills the buffer with the next chunk of the file, or fails. */
static gboolean
put_fill(X2UploadPut *put)
{
	gsize want = x2_upload_send_want(&put->send, put->buf_cap);
	gsize got = fread(put->buf, 1, want, put->fp);

	if (got != want) {
		put_failf(put, "%s", _("the file changed while it was uploaded"));
		return FALSE;
	}
	x2_upload_send_did_read(&put->send, got);
	put->buf_off = 0;
	put->buf_len = got;
	return TRUE;
}

/* Writes for one main loop turn.  FALSE when the PUT has ended. */
static gboolean
put_write(X2UploadPut *put)
{
	gsize burst = 0;

	/* from here, not from the connect callback: libpurple is still in it */
	if (!put->started) {
		put->started = TRUE;
		if (!put_report(put, 0))
			return FALSE;
	}
	while (burst < X2_UPLOAD_BURST && !x2_upload_send_done(&put->send)) {
		gssize n;

		if (put->buf_off == put->buf_len && !put_fill(put))
			return FALSE;
		n = put_send(put, put->buf + put->buf_off,
			put->buf_len - put->buf_off);
		if (n < 0 && errno == EAGAIN)
			return TRUE;
		if (n < 0) {
			/* the server may have answered and closed: look for it */
			put->write_failed = TRUE;
			put->write_errno = errno;
			put_watch(put, PURPLE_INPUT_READ);
			return put_read(put);
		}
		put->buf_off += (gsize)n;
		burst += (gsize)n;
		x2_upload_send_did_write(&put->send, (gsize)n);
		if (x2_upload_send_progress_due(&put->send) &&
		    !put_report(put, x2_upload_send_body_sent(&put->send)))
			return FALSE;
	}
	if (x2_upload_send_done(&put->send)) {
		/* all sent: only the answer is left (it may be here already) */
		put_watch(put, PURPLE_INPUT_READ);
		return put_read(put);
	}
	return TRUE;
}

static void
put_io_cb(gpointer data, gint source, PurpleInputCondition cond)
{
	X2UploadPut *put = data;

	if ((cond & PURPLE_INPUT_READ) && !put_read(put))
		return;
	if ((cond & PURPLE_INPUT_WRITE) && !put->write_failed &&
	    !x2_upload_send_done(&put->send))
		put_write(put);
}

/* The connection is up: start sending. */
static void
put_connected(X2UploadPut *put, int fd)
{
	int flags = fcntl(fd, F_GETFL);

	put->fd = fd;
	if (flags >= 0)
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	put_watch(put, PURPLE_INPUT_READ | PURPLE_INPUT_WRITE);
}

static void
put_connect_cb(gpointer data, gint source, const gchar *error_message)
{
	X2UploadPut *put = data;

	put->connect = NULL;
	if (source < 0) {
		put_failf(put, _("could not connect to %s: %s"), put->host,
			error_message ? error_message : _("unknown error"));
		return;
	}
	put_connected(put, source);
}

static void
put_ssl_connect_cb(gpointer data, PurpleSslConnection *gsc,
                   PurpleInputCondition cond)
{
	X2UploadPut *put = data;

	put->ssl = gsc;
	put_connected(put, gsc->fd);
}

static void
put_ssl_error_cb(PurpleSslConnection *gsc, PurpleSslErrorType error,
                 gpointer data)
{
	X2UploadPut *put = data;

	/* libpurple frees the connection once we return */
	put->ssl = NULL;
	put_failf(put, _("could not make a secure connection to %s: %s"),
		put->host, purple_ssl_strerror(error));
}

X2UploadPut *
x2_upload_put_start(PurpleAccount *account, const X2UploadSlot *slot,
                    const char *path, guint64 size, const char *mime,
                    X2UploadPutProgress progress, X2UploadPutDone done,
                    gpointer data, GError **error)
{
	const X2UploadUrl *u = &slot->put_url;
	X2UploadPut *put;
	char *head;

	g_return_val_if_fail(progress != NULL && done != NULL, NULL);
	if (u->https && !purple_ssl_is_supported()) {
		g_set_error_literal(error, X2_UPLOAD_ERROR, X2_UPLOAD_ERROR_URL,
			_("this build of libpurple has no TLS support"));
		return NULL;
	}
	put = g_new0(X2UploadPut, 1);
	put->progress_cb = progress;
	put->done_cb = done;
	put->data = data;
	put->host = g_strdup(u->host);
	put->https = u->https;
	put->fd = -1;

	put->fp = g_fopen(path, "rb");
	if (put->fp == NULL) {
		g_set_error(error, X2_UPLOAD_ERROR, X2_UPLOAD_ERROR_SIZE,
			_("cannot read the file: %s"), g_strerror(errno));
		put_destroy(put);
		return NULL;
	}
	head = x2_upload_put_head(slot, size, mime,
		"purple-xmpp2/" XMPP2_VERSION);
	x2_upload_send_init(&put->send, strlen(head), size);
	put->buf_cap = MAX(X2_UPLOAD_CHUNK, put->send.head_len);
	put->buf = g_malloc(put->buf_cap);
	memcpy(put->buf, head, put->send.head_len);
	put->buf_len = put->send.head_len;
	g_free(head);
	x2_upload_resp_init(&put->resp);

	put->starting = TRUE;
	if (u->https)
		put->ssl = purple_ssl_connect(account, u->host, u->port,
			put_ssl_connect_cb, put_ssl_error_cb, put);
	else
		put->connect = purple_proxy_connect(NULL, account, u->host,
			u->port, put_connect_cb, put);
	put->starting = FALSE;

	if (put->early_error != NULL || (put->ssl == NULL && put->connect == NULL &&
	    put->fd < 0)) {
		g_set_error(error, X2_UPLOAD_ERROR, X2_UPLOAD_ERROR_URL,
			_("could not connect to %s: %s"), put->host,
			put->early_error ? put->early_error : _("unknown error"));
		/* a connection that failed under us is already gone */
		put->ssl = NULL;
		put->connect = NULL;
		put_destroy(put);
		return NULL;
	}
	return put;
}
