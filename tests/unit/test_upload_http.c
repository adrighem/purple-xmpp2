/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/* Unit tests of core/upload_http.c: chunk accounting of the streaming PUT,
 * progress steps, the response status reader, and the TLS decision that
 * comes from the parsed PUT URL. */
#include <string.h>
#include <glib.h>

#include "x2test.h"

#include "upload_http.h"
#include "upload_proto.h"

/* Drives the accounting like the socket code does: read a chunk, offer it to
 * a socket that takes at most @a take bytes per call.  Returns the number of
 * progress reports and checks that the totals add up. */
static guint
run_send(gsize head_len, guint64 size, gsize cap, gsize take,
         guint64 *last_report)
{
	X2UploadSend s;
	gsize pending = head_len;  /* the buffer starts with the head */
	guint reports = 0;
	guint64 prev = 0;

	x2_upload_send_init(&s, head_len, size);
	*last_report = 0;
	while (!x2_upload_send_done(&s)) {
		gsize n;

		if (pending == 0) {
			pending = x2_upload_send_want(&s, cap);
			g_assert_cmpuint(pending, >, 0);
			g_assert_cmpuint(pending, <=, cap);
			x2_upload_send_did_read(&s, pending);
		}
		n = MIN(pending, take);
		x2_upload_send_did_write(&s, n);
		pending -= n;
		if (x2_upload_send_progress_due(&s)) {
			guint64 now = x2_upload_send_body_sent(&s);

			g_assert_cmpuint(now, >, prev);
			prev = now;
			*last_report = now;
			reports++;
		}
	}
	g_assert_cmpuint(s.body_read, ==, size);
	g_assert_cmpuint(x2_upload_send_body_sent(&s), ==, size);
	g_assert_cmpuint(s.stream_sent, ==, head_len + size);
	g_assert_cmpuint(x2_upload_send_want(&s, cap), ==, 0);
	return reports;
}

static void
test_send_accounting_exact(void)
{
	guint64 last;

	/* 1 MiB in 64 KiB chunks written whole: 16 chunks, 16 reports (1% of
	 * 1 MiB is below the 64 KiB minimum step) */
	g_assert_cmpuint(run_send(300, 1 << 20, X2_UPLOAD_CHUNK,
		X2_UPLOAD_CHUNK, &last), ==, 16);
	g_assert_cmpuint(last, ==, 1 << 20);
}

static void
test_send_partial_writes(void)
{
	guint64 last;

	/* the socket takes 1000 bytes at a time: totals still add up and the
	 * reports stay in steps, the last one at the end */
	guint reports = run_send(777, 5 * 1024 * 1024 + 13, X2_UPLOAD_CHUNK,
		1000, &last);

	g_assert_cmpuint(last, ==, 5 * 1024 * 1024 + 13);
	g_assert_cmpuint(reports, <=, 101);
	g_assert_cmpuint(reports, >=, 50);
}

static void
test_send_tiny_and_head_larger_than_chunk(void)
{
	guint64 last;

	g_assert_cmpuint(run_send(100, 1, X2_UPLOAD_CHUNK, 4096, &last), ==, 1);
	g_assert_cmpuint(last, ==, 1);
	/* a head bigger than the chunk size is written as it is */
	g_assert_cmpuint(run_send(70000, 10, X2_UPLOAD_CHUNK, 500, &last), ==, 1);
	/* big file: one report per percent at most */
	g_assert_cmpuint(run_send(200, (guint64)50 << 20, X2_UPLOAD_CHUNK,
		X2_UPLOAD_CHUNK, &last), <=, 101);
}

static void
test_send_no_progress_in_head(void)
{
	X2UploadSend s;

	x2_upload_send_init(&s, 500, 1000);
	x2_upload_send_did_write(&s, 500);
	g_assert_cmpuint(x2_upload_send_body_sent(&s), ==, 0);
	g_assert_false(x2_upload_send_progress_due(&s));
	g_assert_false(x2_upload_send_done(&s));
}

static void
test_send_want_is_bounded(void)
{
	X2UploadSend s;

	x2_upload_send_init(&s, 0, (guint64)10 << 30);  /* 10 GiB */
	g_assert_cmpuint(x2_upload_send_want(&s, 65536), ==, 65536);
	x2_upload_send_init(&s, 0, 100);
	g_assert_cmpuint(x2_upload_send_want(&s, 65536), ==, 100);
	x2_upload_send_did_read(&s, 60);
	g_assert_cmpuint(x2_upload_send_want(&s, 30), ==, 30);
	g_assert_cmpuint(x2_upload_send_want(&s, 65536), ==, 40);
}

static X2UploadRespState
feed_str(X2UploadResp *r, const char *s)
{
	return x2_upload_resp_feed(r, s, strlen(s));
}

static void
test_resp_status_line(void)
{
	X2UploadResp r;

	x2_upload_resp_init(&r);
	g_assert_cmpint(feed_str(&r, "HTTP/1.1 201 Cre"), ==, X2_UPLOAD_RESP_MORE);
	g_assert_cmpint(feed_str(&r, "ated\r\nServer: x\r\n"), ==,
		X2_UPLOAD_RESP_DONE);
	g_assert_cmpint(r.code, ==, 201);

	x2_upload_resp_init(&r);
	g_assert_cmpint(feed_str(&r, "HTTP/1.0 413 Payload Too Large\r\n\r\n"), ==,
		X2_UPLOAD_RESP_DONE);
	g_assert_cmpint(r.code, ==, 413);

	/* one byte at a time, bare LF, no reason phrase */
	x2_upload_resp_init(&r);
	{
		const char *in = "HTTP/1.1 204\n";
		X2UploadRespState st = X2_UPLOAD_RESP_MORE;
		gsize i;

		for (i = 0; in[i] != '\0'; i++) {
			g_assert_cmpint(st, ==, X2_UPLOAD_RESP_MORE);
			st = x2_upload_resp_feed(&r, in + i, 1);
		}
		g_assert_cmpint(st, ==, X2_UPLOAD_RESP_DONE);
		g_assert_cmpint(r.code, ==, 204);
	}
}

static void
test_resp_interim_skipped(void)
{
	X2UploadResp r;

	x2_upload_resp_init(&r);
	g_assert_cmpint(feed_str(&r, "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 2"),
		==, X2_UPLOAD_RESP_MORE);
	g_assert_cmpint(feed_str(&r, "01 Created\r\n"), ==, X2_UPLOAD_RESP_DONE);
	g_assert_cmpint(r.code, ==, 201);

	/* 100 with headers, split before its blank line */
	x2_upload_resp_init(&r);
	g_assert_cmpint(feed_str(&r, "HTTP/1.1 102 Processing\r\nX: y\r\n"), ==,
		X2_UPLOAD_RESP_MORE);
	g_assert_cmpint(feed_str(&r, "\r\nHTTP/1.1 200 OK\r\n"), ==,
		X2_UPLOAD_RESP_DONE);
	g_assert_cmpint(r.code, ==, 200);

	/* endless interim responses are refused */
	x2_upload_resp_init(&r);
	g_assert_cmpint(feed_str(&r,
		"HTTP/1.1 100 a\r\n\r\nHTTP/1.1 100 b\r\n\r\n"
		"HTTP/1.1 100 c\r\n\r\nHTTP/1.1 100 d\r\n\r\n"
		"HTTP/1.1 100 e\r\n\r\n"), ==, X2_UPLOAD_RESP_BAD);
}

static void
test_resp_garbage_and_overlong(void)
{
	X2UploadResp r;
	char junk[X2_UPLOAD_RESP_MAX + 100];

	x2_upload_resp_init(&r);
	g_assert_cmpint(feed_str(&r, "<html>nope</html>\r\n"), ==,
		X2_UPLOAD_RESP_BAD);
	x2_upload_resp_init(&r);
	g_assert_cmpint(feed_str(&r, "HTTP/2 200\r\n"), ==, X2_UPLOAD_RESP_BAD);
	x2_upload_resp_init(&r);
	g_assert_cmpint(feed_str(&r, "HTTP/1.1 099 x\r\n"), ==,
		X2_UPLOAD_RESP_BAD);

	/* no line end within the limit: bad, and memory stays bounded */
	memset(junk, 'a', sizeof(junk));
	x2_upload_resp_init(&r);
	g_assert_cmpint(x2_upload_resp_feed(&r, junk, sizeof(junk)), ==,
		X2_UPLOAD_RESP_BAD);
	g_assert_cmpuint(r.len, <=, X2_UPLOAD_RESP_MAX);
}

static void
test_status_classes(void)
{
	g_assert_cmpint(x2_upload_status_class(200), ==, X2_UPLOAD_STATUS_OK);
	g_assert_cmpint(x2_upload_status_class(201), ==, X2_UPLOAD_STATUS_OK);
	g_assert_cmpint(x2_upload_status_class(204), ==, X2_UPLOAD_STATUS_OK);
	g_assert_cmpint(x2_upload_status_class(202), ==, X2_UPLOAD_STATUS_OTHER);
	g_assert_cmpint(x2_upload_status_class(401), ==, X2_UPLOAD_STATUS_REFUSED);
	g_assert_cmpint(x2_upload_status_class(403), ==, X2_UPLOAD_STATUS_REFUSED);
	g_assert_cmpint(x2_upload_status_class(413), ==,
		X2_UPLOAD_STATUS_TOO_LARGE);
	g_assert_cmpint(x2_upload_status_class(301), ==,
		X2_UPLOAD_STATUS_REDIRECT);
	g_assert_cmpint(x2_upload_status_class(307), ==,
		X2_UPLOAD_STATUS_REDIRECT);
	g_assert_cmpint(x2_upload_status_class(308), ==,
		X2_UPLOAD_STATUS_REDIRECT);
	g_assert_cmpint(x2_upload_status_class(400), ==, X2_UPLOAD_STATUS_OTHER);
	g_assert_cmpint(x2_upload_status_class(500), ==, X2_UPLOAD_STATUS_OTHER);
	g_assert_cmpint(x2_upload_status_class(507), ==, X2_UPLOAD_STATUS_OTHER);
}

/* The TLS path is chosen from the URL scheme alone (upload_put.c): https
 * means purple_ssl_connect() to the URL host (SNI and certificate name) and
 * port, http means purple_proxy_connect().  There is no way to turn a
 * https slot into plain http, except the account option for http slots. */
static void
test_tls_decision_from_url(void)
{
	X2UploadUrl u;
	GError *err = NULL;

	g_assert_true(x2_upload_url_parse("https://up.example.org/p?x=1", &u,
		&err));
	g_assert_true(u.https);
	g_assert_cmpstr(u.host, ==, "up.example.org");
	g_assert_cmpint(u.port, ==, 443);
	x2_upload_url_clear(&u);

	g_assert_true(x2_upload_url_parse("https://UP.example.org:8443/p", &u,
		&err));
	g_assert_true(u.https);
	g_assert_cmpint(u.port, ==, 8443);
	x2_upload_url_clear(&u);

	g_assert_true(x2_upload_url_parse("http://up.example.org/p", &u, &err));
	g_assert_false(u.https);
	g_assert_cmpint(u.port, ==, 80);
	x2_upload_url_clear(&u);
	g_assert_no_error(err);
}

int
main(int argc, char **argv)
{
	x2_test_init(&argc, &argv);
	g_test_add_func("/upload-http/send/exact", test_send_accounting_exact);
	g_test_add_func("/upload-http/send/partial-writes", test_send_partial_writes);
	g_test_add_func("/upload-http/send/tiny-and-big-head",
		test_send_tiny_and_head_larger_than_chunk);
	g_test_add_func("/upload-http/send/head-is-no-progress",
		test_send_no_progress_in_head);
	g_test_add_func("/upload-http/send/want", test_send_want_is_bounded);
	g_test_add_func("/upload-http/resp/status-line", test_resp_status_line);
	g_test_add_func("/upload-http/resp/interim", test_resp_interim_skipped);
	g_test_add_func("/upload-http/resp/garbage", test_resp_garbage_and_overlong);
	g_test_add_func("/upload-http/status-class", test_status_classes);
	g_test_add_func("/upload-http/tls-decision", test_tls_decision_from_url);
	return g_test_run();
}
