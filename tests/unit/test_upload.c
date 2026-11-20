/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/* Unit tests of core/upload_proto.c: XEP-0363 discovery, slot vetting, PUT
 * request building, URL parsing (http and https), upload message. */
#include <string.h>
#include <glib.h>

#include "x2test.h"

#include "upload_proto.h"

static xmlnode *
parse(const char *xml)
{
	xmlnode *n = xmlnode_from_str(xml, -1);

	g_assert_nonnull(n);
	return n;
}

#define DISCO_OPEN "<query xmlns='http://jabber.org/protocol/disco#info'>"

static void
test_disco_max_size(void)
{
	xmlnode *q = parse(DISCO_OPEN
		"<feature var='urn:xmpp:http:upload:0'/>"
		"<x xmlns='jabber:x:data' type='result'>"
		"<field var='FORM_TYPE' type='hidden'>"
		"<value>urn:xmpp:http:upload:0</value></field>"
		"<field var='max-file-size'><value>10485760</value></field>"
		"</x></query>");
	guint64 max = 1;

	g_assert_true(x2_upload_disco_parse(q, &max));
	g_assert_cmpuint(max, ==, 10485760);
	xmlnode_free(q);
}

static void
test_disco_no_feature_no_size_foreign_form(void)
{
	guint64 max = 1;
	xmlnode *q = parse(DISCO_OPEN "<feature var='urn:other'/></query>");

	g_assert_false(x2_upload_disco_parse(q, &max));
	xmlnode_free(q);

	q = parse(DISCO_OPEN "<feature var='urn:xmpp:http:upload:0'/></query>");
	g_assert_true(x2_upload_disco_parse(q, &max));
	g_assert_cmpuint(max, ==, 0);
	xmlnode_free(q);

	/* max-file-size of another form type, and garbage, are ignored */
	q = parse(DISCO_OPEN "<feature var='urn:xmpp:http:upload:0'/>"
		"<x xmlns='jabber:x:data' type='result'>"
		"<field var='FORM_TYPE'><value>urn:other</value></field>"
		"<field var='max-file-size'><value>5</value></field></x>"
		"<x xmlns='jabber:x:data' type='result'>"
		"<field var='FORM_TYPE'><value>urn:xmpp:http:upload:0</value></field>"
		"<field var='max-file-size'><value>12x</value></field></x></query>");
	g_assert_true(x2_upload_disco_parse(q, &max));
	g_assert_cmpuint(max, ==, 0);
	xmlnode_free(q);
}

static void
test_size_checks(void)
{
	GError *err = NULL;
	guint64 mib = 1024 * 1024;

	g_assert_true(x2_upload_check_size(5 * mib, 10 * mib, &err));
	g_assert_true(x2_upload_check_size(10 * mib, 10 * mib, &err));
	g_assert_no_error(err);

	g_assert_false(x2_upload_check_size(10 * mib + 1, 10 * mib, &err));
	g_assert_error(err, X2_UPLOAD_ERROR, X2_UPLOAD_ERROR_SIZE);
	g_assert_nonnull(strstr(err->message, "to this server"));
	g_clear_error(&err);

	g_assert_false(x2_upload_check_size(0, 10 * mib, &err));
	g_assert_error(err, X2_UPLOAD_ERROR, X2_UPLOAD_ERROR_SIZE);
	g_clear_error(&err);

	/* no cap of ours: the file is streamed, only the server limit counts */
	g_assert_true(x2_upload_check_size(100 * mib, 0, &err));
	g_assert_true(x2_upload_check_size(8192 * mib, 0, &err));
	g_assert_true(x2_upload_check_size(500 * mib, 500 * mib, &err));
	g_assert_no_error(err);
	g_assert_false(x2_upload_check_size(500 * mib + 1, 500 * mib, &err));
	g_clear_error(&err);
}

static void
test_filename_and_mime(void)
{
	char *n = x2_upload_filename("/tmp/dir/photo.PNG");

	g_assert_cmpstr(n, ==, "photo.PNG");
	g_free(n);
	n = x2_upload_filename("/");
	g_assert_cmpstr(n, ==, "file");
	g_free(n);
	n = x2_upload_filename("a\nb\r.txt");
	g_assert_cmpstr(n, ==, "a_b_.txt");
	g_free(n);

	g_assert_cmpstr(x2_upload_guess_mime("a.PNG"), ==, "image/png");
	g_assert_cmpstr(x2_upload_guess_mime("a.tar.gz"), ==,
		"application/octet-stream");
	g_assert_cmpstr(x2_upload_guess_mime("noext"), ==,
		"application/octet-stream");
}

static void
test_request(void)
{
	xmlnode *r = x2_upload_request_new("a.png", 1048576, "image/png");

	g_assert_cmpstr(xmlnode_get_namespace(r), ==, X2_NS_HTTP_UPLOAD);
	g_assert_cmpstr(xmlnode_get_attrib(r, "filename"), ==, "a.png");
	g_assert_cmpstr(xmlnode_get_attrib(r, "size"), ==, "1048576");
	g_assert_cmpstr(xmlnode_get_attrib(r, "content-type"), ==, "image/png");
	xmlnode_free(r);
}

static void
test_url_parse(void)
{
	X2UploadUrl u;
	GError *err = NULL;

	g_assert_true(x2_upload_url_parse("https://up.example/a/b?c=d#frag", &u, &err));
	g_assert_true(u.https);
	g_assert_cmpstr(u.host, ==, "up.example");
	g_assert_cmpint(u.port, ==, 443);
	g_assert_cmpstr(u.path, ==, "/a/b?c=d");
	x2_upload_url_clear(&u);

	g_assert_true(x2_upload_url_parse("HTTP://localhost:15280/file_share/x", &u, &err));
	g_assert_false(u.https);
	g_assert_cmpint(u.port, ==, 15280);
	g_assert_cmpstr(u.path, ==, "/file_share/x");
	x2_upload_url_clear(&u);

	g_assert_true(x2_upload_url_parse("https://h.example:8443", &u, &err));
	g_assert_cmpint(u.port, ==, 8443);
	g_assert_cmpstr(u.path, ==, "/");
	x2_upload_url_clear(&u);

	g_assert_true(x2_upload_url_parse("https://h.example?q=1", &u, &err));
	g_assert_cmpstr(u.path, ==, "/?q=1");
	x2_upload_url_clear(&u);
	g_assert_no_error(err);
}

static void
test_url_parse_rejects(void)
{
	static const char *const bad[] = {
		"", "ftp://h/x", "//h/x", "https://", "https://h:0/x",
		"https://h:65536/x", "https://h:/x", "https://h:80a/x",
		"https://u:p@h/x", "https://[::1]/x", "https://h/x y",
		"https://h/x\r\nHost: evil", "https://h .example/x", "https://h$/x",
		NULL
	};
	int i;

	for (i = 0; bad[i]; i++) {
		X2UploadUrl u;
		GError *err = NULL;

		g_assert_false(x2_upload_url_parse(bad[i], &u, &err));
		g_assert_error(err, X2_UPLOAD_ERROR, X2_UPLOAD_ERROR_URL);
		g_assert_null(u.host);
		g_clear_error(&err);
	}
}

#define SLOT(put_url, headers) \
	"<slot xmlns='urn:xmpp:http:upload:0'><put url='" put_url "'>" headers \
	"</put><get url='https://up.example/get/f.png'/></slot>"

static void
test_slot_https_headers(void)
{
	xmlnode *x = parse(SLOT("https://up.example/put/f.png",
		"<header name='authorization'>Basic Zm9v</header>"
		"<header name='Cookie'>a=b</header>"
		"<header name='EXPIRES'>Wed, 21 Oct 2026 07:28:00 GMT</header>"));
	GError *err = NULL;
	X2UploadSlot *s = x2_upload_slot_parse(x, FALSE, &err);
	X2UploadHeader *h;

	g_assert_no_error(err);
	g_assert_nonnull(s);
	g_assert_cmpstr(s->get, ==, "https://up.example/get/f.png");
	g_assert_cmpuint(s->headers->len, ==, 3);
	h = g_ptr_array_index(s->headers, 0);
	g_assert_cmpstr(h->name, ==, "Authorization");
	g_assert_cmpstr(h->value, ==, "Basic Zm9v");
	h = g_ptr_array_index(s->headers, 2);
	g_assert_cmpstr(h->name, ==, "Expires");
	x2_upload_slot_free(s);
	xmlnode_free(x);
}

static void
test_slot_drops_other_headers(void)
{
	xmlnode *x = parse(SLOT("https://up.example/put/f.png",
		"<header name='Host'>evil.example</header>"
		"<header name='Content-Length'>1</header>"
		"<header name='Transfer-Encoding'>chunked</header>"
		"<header name='X-Custom'>1</header>"
		"<header name='Cookie'>a=b</header>"));
	X2UploadSlot *s = x2_upload_slot_parse(x, FALSE, NULL);
	char *head;

	g_assert_nonnull(s);
	g_assert_cmpuint(s->headers->len, ==, 1);
	g_assert_cmpuint(s->dropped, ==, 4);
	head = x2_upload_put_head(s, 3, "text/plain", "ua/1");
	g_assert_null(strstr(head, "evil.example"));
	g_assert_null(strstr(head, "chunked"));
	g_assert_null(strstr(head, "X-Custom"));
	g_free(head);
	x2_upload_slot_free(s);
	xmlnode_free(x);
}

static void
test_slot_rejects_crlf_header_values(void)
{
	static const char *const bad[] = {
		"<header name='Authorization'>x&#13;&#10;Host: evil</header>",
		"<header name='Cookie'>a&#10;b</header>",
		"<header name='Expires'>a&#13;b</header>",
		"<header name='Cookie'>a&#0;b</header>",
		"<header name='Authorization'>a&#127;</header>",
		NULL
	};
	int i;

	for (i = 0; bad[i]; i++) {
		char *xml = g_strdup_printf(SLOT("https://up.example/p", "%s"), bad[i]);
		xmlnode *x = xmlnode_from_str(xml, -1);
		GError *err = NULL;

		g_free(xml);
		if (x == NULL)	/* the XML parser itself refuses (NUL) */
			continue;
		g_assert_null(x2_upload_slot_parse(x, FALSE, &err));
		g_assert_error(err, X2_UPLOAD_ERROR, X2_UPLOAD_ERROR_HEADER);
		/* the secret-bearing name is in the message, never the value */
		g_assert_null(strstr(err->message, "evil"));
		g_clear_error(&err);
		xmlnode_free(x);
	}
}

static void
test_slot_trims_surrounding_whitespace(void)
{
	xmlnode *x = parse(SLOT("https://up.example/p",
		"<header name='Authorization'>\n  Bearer tok\n </header>"));
	X2UploadSlot *s = x2_upload_slot_parse(x, FALSE, NULL);
	X2UploadHeader *h;

	g_assert_nonnull(s);
	h = g_ptr_array_index(s->headers, 0);
	g_assert_cmpstr(h->value, ==, "Bearer tok");
	x2_upload_slot_free(s);
	xmlnode_free(x);
}

static void
test_slot_plain_http_needs_opt_in(void)
{
	xmlnode *x = parse(
		"<slot xmlns='urn:xmpp:http:upload:0'>"
		"<put url='http://localhost:15280/put'/>"
		"<get url='http://localhost:15280/get'/></slot>");
	GError *err = NULL;
	X2UploadSlot *s = x2_upload_slot_parse(x, FALSE, &err);

	g_assert_null(s);
	g_assert_error(err, X2_UPLOAD_ERROR, X2_UPLOAD_ERROR_URL);
	g_clear_error(&err);
	s = x2_upload_slot_parse(x, TRUE, &err);
	g_assert_no_error(err);
	g_assert_nonnull(s);
	g_assert_false(s->put_url.https);
	x2_upload_slot_free(s);
	xmlnode_free(x);

	/* https PUT but http GET is refused too */
	x = parse("<slot xmlns='urn:xmpp:http:upload:0'>"
		"<put url='https://h/put'/><get url='http://h/get'/></slot>");
	g_assert_null(x2_upload_slot_parse(x, FALSE, &err));
	g_clear_error(&err);
	xmlnode_free(x);
}

static void
test_slot_malformed(void)
{
	GError *err = NULL;
	xmlnode *x = parse("<slot xmlns='urn:xmpp:http:upload:0'>"
		"<put url='https://h/put'/></slot>");

	g_assert_null(x2_upload_slot_parse(x, FALSE, &err));
	g_assert_error(err, X2_UPLOAD_ERROR, X2_UPLOAD_ERROR_SLOT);
	g_clear_error(&err);
	xmlnode_free(x);

	x = parse("<slot xmlns='urn:xmpp:http:upload:0'><get url='https://h/g'/></slot>");
	g_assert_null(x2_upload_slot_parse(x, FALSE, &err));
	g_assert_error(err, X2_UPLOAD_ERROR, X2_UPLOAD_ERROR_SLOT);
	g_clear_error(&err);
	xmlnode_free(x);
}

static void
test_put_head(void)
{
	xmlnode *x = parse(SLOT("https://up.example:8443/put/f.png?v=1",
		"<header name='Authorization'>Basic Zm9v</header>"));
	X2UploadSlot *s = x2_upload_slot_parse(x, FALSE, NULL);
	char *head = x2_upload_put_head(s, 1048576, "image/png", "purple-xmpp2/1");

	g_assert_true(g_str_has_prefix(head, "PUT /put/f.png?v=1 HTTP/1.1\r\n"));
	g_assert_nonnull(strstr(head, "\r\nHost: up.example:8443\r\n"));
	g_assert_nonnull(strstr(head, "\r\nContent-Type: image/png\r\n"));
	g_assert_nonnull(strstr(head, "\r\nContent-Length: 1048576\r\n"));
	g_assert_nonnull(strstr(head, "\r\nAuthorization: Basic Zm9v\r\n"));
	g_assert_nonnull(strstr(head, "\r\nUser-Agent: purple-xmpp2/1\r\n"));
	g_assert_true(g_str_has_suffix(head, "\r\n\r\n"));
	g_free(head);
	x2_upload_slot_free(s);
	xmlnode_free(x);
}

static void
test_put_head_default_port_and_bad_mime(void)
{
	xmlnode *x = parse(SLOT("https://up.example/put", ""));
	X2UploadSlot *s = x2_upload_slot_parse(x, FALSE, NULL);
	char *head = x2_upload_put_head(s, 1, "text/plain\r\nX: y", NULL);

	g_assert_nonnull(strstr(head, "\r\nHost: up.example\r\n"));
	g_assert_nonnull(strstr(head,
		"\r\nContent-Type: application/octet-stream\r\n"));
	g_assert_null(strstr(head, "X: y"));
	g_assert_null(strstr(head, "User-Agent"));
	g_free(head);
	x2_upload_slot_free(s);
	xmlnode_free(x);
}

static void
test_put_status_ok(void)
{
	g_assert_true(x2_upload_put_status_ok(200));
	g_assert_true(x2_upload_put_status_ok(201));
	g_assert_true(x2_upload_put_status_ok(204));
	g_assert_false(x2_upload_put_status_ok(202));
	g_assert_false(x2_upload_put_status_ok(206));
	g_assert_false(x2_upload_put_status_ok(301));
	g_assert_false(x2_upload_put_status_ok(307));
	g_assert_false(x2_upload_put_status_ok(400));
	g_assert_false(x2_upload_put_status_ok(403));
	g_assert_false(x2_upload_put_status_ok(413));
	g_assert_false(x2_upload_put_status_ok(500));
	g_assert_false(x2_upload_put_status_ok(0));
}

static void
test_status_parse(void)
{
	int code = 0;

	g_assert_true(x2_upload_status_parse("HTTP/1.1 201 Created\r\n\r\n", &code));
	g_assert_cmpint(code, ==, 201);
	g_assert_true(x2_upload_status_parse("HTTP/1.0 403 Forbidden", &code));
	g_assert_cmpint(code, ==, 403);
	g_assert_false(x2_upload_status_parse("", &code));
	g_assert_false(x2_upload_status_parse(NULL, &code));
	g_assert_false(x2_upload_status_parse("HTTP/1.1 20", &code));
	g_assert_false(x2_upload_status_parse("<html>", &code));
	g_assert_false(x2_upload_status_parse("HTTP/2 200", &code));
}

static void
test_message(void)
{
	xmlnode *m = x2_upload_message_new("bob@x", FALSE, "https://u/g?a=1&b=2");
	xmlnode *x, *body;
	char *text;

	g_assert_cmpstr(xmlnode_get_attrib(m, "type"), ==, "chat");
	g_assert_cmpstr(xmlnode_get_attrib(m, "to"), ==, "bob@x");
	body = xmlnode_get_child(m, "body");
	text = xmlnode_get_data(body);
	g_assert_cmpstr(text, ==, "https://u/g?a=1&b=2");
	g_free(text);
	x = xmlnode_get_child_with_namespace(m, "x", X2_NS_OOB_X);
	g_assert_nonnull(x);
	text = xmlnode_get_data(xmlnode_get_child(x, "url"));
	g_assert_cmpstr(text, ==, "https://u/g?a=1&b=2");
	g_free(text);
	xmlnode_free(m);

	m = x2_upload_message_new("room@c", TRUE, "https://u/g");
	g_assert_cmpstr(xmlnode_get_attrib(m, "type"), ==, "groupchat");
	xmlnode_free(m);
}

int
main(int argc, char **argv)
{
	x2_test_init(&argc, &argv);
	g_test_add_func("/upload/disco/max-size", test_disco_max_size);
	g_test_add_func("/upload/disco/absent-and-foreign",
		test_disco_no_feature_no_size_foreign_form);
	g_test_add_func("/upload/size", test_size_checks);
	g_test_add_func("/upload/filename-mime", test_filename_and_mime);
	g_test_add_func("/upload/request", test_request);
	g_test_add_func("/upload/url/parse", test_url_parse);
	g_test_add_func("/upload/url/rejects", test_url_parse_rejects);
	g_test_add_func("/upload/slot/headers", test_slot_https_headers);
	g_test_add_func("/upload/slot/drops-other-headers",
		test_slot_drops_other_headers);
	g_test_add_func("/upload/slot/rejects-crlf", test_slot_rejects_crlf_header_values);
	g_test_add_func("/upload/slot/trims", test_slot_trims_surrounding_whitespace);
	g_test_add_func("/upload/slot/plain-http", test_slot_plain_http_needs_opt_in);
	g_test_add_func("/upload/slot/malformed", test_slot_malformed);
	g_test_add_func("/upload/put-head", test_put_head);
	g_test_add_func("/upload/put-head/defaults", test_put_head_default_port_and_bad_mime);
	g_test_add_func("/upload/status", test_status_parse);
	g_test_add_func("/upload/put-status-ok", test_put_status_ok);
	g_test_add_func("/upload/message", test_message);
	return g_test_run();
}
