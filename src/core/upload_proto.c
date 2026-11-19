/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file upload_proto.c XEP-0363 HTTP File Upload (pure logic)
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "upload_proto.h"

G_DEFINE_QUARK(x2-upload-error-quark, x2_upload_error)

/* Slot headers a service may ask us to send (XEP-0363, section 4) */
static const char *const allowed_headers[] = {
	"Authorization", "Cookie", "Expires", NULL
};

static const struct {
	const char *ext;
	const char *mime;
} mime_table[] = {
	{ "jpg", "image/jpeg" }, { "jpeg", "image/jpeg" },
	{ "png", "image/png" }, { "gif", "image/gif" },
	{ "webp", "image/webp" }, { "svg", "image/svg+xml" },
	{ "bmp", "image/bmp" },
	{ "mp4", "video/mp4" }, { "webm", "video/webm" },
	{ "mkv", "video/x-matroska" }, { "mov", "video/quicktime" },
	{ "mp3", "audio/mpeg" }, { "ogg", "audio/ogg" },
	{ "oga", "audio/ogg" }, { "opus", "audio/opus" },
	{ "wav", "audio/wav" }, { "flac", "audio/flac" },
	{ "m4a", "audio/mp4" },
	{ "pdf", "application/pdf" }, { "zip", "application/zip" },
	{ "json", "application/json" }, { "txt", "text/plain" },
	{ NULL, NULL }
};

/* Text of a child, whitespace trimmed; NULL if absent. Free with g_free(). */
static char *
child_text(const xmlnode *parent, const char *name)
{
	xmlnode *child = xmlnode_get_child(parent, name);
	char *text, *ret;

	if (child == NULL)
		return NULL;
	text = xmlnode_get_data(child);
	if (text == NULL)
		return NULL;
	ret = g_strstrip(g_strdup(text));
	g_free(text);
	return ret;
}

static gboolean
has_control_char(const char *s, gboolean allow_tab)
{
	for (; *s; s++) {
		if (allow_tab && *s == '\t')
			continue;
		if ((guchar)*s < 0x20 || *s == 0x7f)
			return TRUE;
	}
	return FALSE;
}

gboolean
x2_upload_disco_parse(const xmlnode *query, guint64 *max_size)
{
	gboolean found = FALSE;
	xmlnode *child;

	g_return_val_if_fail(query != NULL && max_size != NULL, FALSE);
	*max_size = 0;

	for (child = xmlnode_get_child(query, "feature"); child;
	     child = xmlnode_get_next_twin(child)) {
		if (g_strcmp0(xmlnode_get_attrib(child, "var"),
		              X2_NS_HTTP_UPLOAD) == 0)
			found = TRUE;
	}
	if (!found)
		return FALSE;

	for (child = xmlnode_get_child_with_namespace(query, "x", "jabber:x:data");
	     child; child = xmlnode_get_next_twin(child)) {
		xmlnode *field;
		gboolean ours = FALSE;
		guint64 size = 0;
		gboolean have_size = FALSE;

		for (field = xmlnode_get_child(child, "field"); field;
		     field = xmlnode_get_next_twin(field)) {
			const char *var = xmlnode_get_attrib(field, "var");
			char *val = child_text(field, "value");

			if (val != NULL && g_strcmp0(var, "FORM_TYPE") == 0) {
				ours = g_strcmp0(val, X2_NS_HTTP_UPLOAD) == 0;
			} else if (val != NULL && g_strcmp0(var, "max-file-size") == 0) {
				char *end = NULL;

				errno = 0;
				size = g_ascii_strtoull(val, &end, 10);
				have_size = errno == 0 && *val != '\0' && *end == '\0';
			}
			g_free(val);
		}
		if (ours && have_size) {
			*max_size = size;
			break;
		}
	}
	return TRUE;
}

guint64
x2_upload_effective_max(guint64 service_max)
{
	if (service_max == 0 || service_max > X2_UPLOAD_HARD_CAP)
		return X2_UPLOAD_HARD_CAP;
	return service_max;
}

gboolean
x2_upload_check_size(guint64 size, guint64 service_max, GError **error)
{
	guint64 max = x2_upload_effective_max(service_max);

	if (size == 0) {
		g_set_error_literal(error, X2_UPLOAD_ERROR, X2_UPLOAD_ERROR_SIZE,
			"The file is empty");
		return FALSE;
	}
	if (size > max) {
		char *have = g_format_size_full(size, G_FORMAT_SIZE_IEC_UNITS);
		char *want = g_format_size_full(max, G_FORMAT_SIZE_IEC_UNITS);

		g_set_error(error, X2_UPLOAD_ERROR, X2_UPLOAD_ERROR_SIZE,
			"The file is %s, but at most %s can be uploaded%s",
			have, want, max == service_max ? " to this server" : "");
		g_free(have);
		g_free(want);
		return FALSE;
	}
	return TRUE;
}

char *
x2_upload_filename(const char *path)
{
	char *base = g_path_get_basename(path ? path : "");
	char *name = g_utf8_make_valid(base, -1);
	char *p;

	g_free(base);
	for (p = name; *p; p++)
		if ((guchar)*p < 0x20 || *p == 0x7f)
			*p = '_';
	if (*name == '\0' || g_strcmp0(name, ".") == 0 ||
	    g_strcmp0(name, G_DIR_SEPARATOR_S) == 0) {
		g_free(name);
		return g_strdup("file");
	}
	return name;
}

const char *
x2_upload_guess_mime(const char *filename)
{
	const char *dot = filename ? strrchr(filename, '.') : NULL;
	int i;

	if (dot == NULL)
		return "application/octet-stream";
	for (i = 0; mime_table[i].ext != NULL; i++)
		if (g_ascii_strcasecmp(dot + 1, mime_table[i].ext) == 0)
			return mime_table[i].mime;
	return "application/octet-stream";
}

xmlnode *
x2_upload_request_new(const char *filename, guint64 size, const char *mime)
{
	xmlnode *request = xmlnode_new("request");
	char *sz = g_strdup_printf("%" G_GUINT64_FORMAT, size);

	xmlnode_set_namespace(request, X2_NS_HTTP_UPLOAD);
	xmlnode_set_attrib(request, "filename", filename);
	xmlnode_set_attrib(request, "size", sz);
	if (mime != NULL)
		xmlnode_set_attrib(request, "content-type", mime);
	g_free(sz);
	return request;
}

static gboolean
url_fail(GError **error, const char *what)
{
	g_set_error(error, X2_UPLOAD_ERROR, X2_UPLOAD_ERROR_URL,
		"Unusable upload URL: %s", what);
	return FALSE;
}

static gboolean
host_valid(const char *host)
{
	const char *p;

	if (*host == '\0')
		return FALSE;
	for (p = host; *p; p++)
		if (!g_ascii_isalnum(*p) && *p != '.' && *p != '-' && *p != '_')
			return FALSE;
	return TRUE;
}

/* Parses the digits after the colon of an authority; 0 on error. */
static int
parse_port(const char *digits)
{
	long n = 0;

	if (*digits == '\0')
		return 0;
	for (; *digits; digits++) {
		if (!g_ascii_isdigit(*digits))
			return 0;
		n = n * 10 + (*digits - '0');
		if (n > 65535)
			return 0;
	}
	return (int)n;
}

gboolean
x2_upload_url_parse(const char *url, X2UploadUrl *out, GError **error)
{
	const char *p, *auth_end, *frag, *colon;
	char *authority, *host, *rest;
	gboolean https;
	int port;

	g_return_val_if_fail(out != NULL, FALSE);
	memset(out, 0, sizeof(*out));
	if (url == NULL || *url == '\0')
		return url_fail(error, "empty");
	if (has_control_char(url, FALSE) || strchr(url, ' ') != NULL)
		return url_fail(error, "contains spaces or control characters");

	if (g_ascii_strncasecmp(url, "https://", 8) == 0) {
		https = TRUE;
		p = url + 8;
	} else if (g_ascii_strncasecmp(url, "http://", 7) == 0) {
		https = FALSE;
		p = url + 7;
	} else {
		return url_fail(error, "not an http or https URL");
	}

	auth_end = p + strcspn(p, "/?#");
	authority = g_strndup(p, auth_end - p);
	if (strchr(authority, '@') != NULL || strchr(authority, '[') != NULL) {
		g_free(authority);
		return url_fail(error, "user info and IPv6 literals are not supported");
	}

	port = https ? 443 : 80;
	colon = strrchr(authority, ':');
	if (colon != NULL) {
		port = parse_port(colon + 1);
		host = g_strndup(authority, colon - authority);
	} else {
		host = g_strdup(authority);
	}
	g_free(authority);
	if (port == 0) {
		g_free(host);
		return url_fail(error, "bad port");
	}
	if (!host_valid(host)) {
		g_free(host);
		return url_fail(error, "bad host name");
	}

	/* path and query, without the fragment, always with a leading slash */
	frag = strchr(auth_end, '#');
	rest = frag ? g_strndup(auth_end, frag - auth_end) : g_strdup(auth_end);
	out->https = https;
	out->host = host;
	out->port = port;
	if (*rest == '/') {
		out->path = rest;
	} else {
		out->path = g_strconcat("/", rest, NULL);
		g_free(rest);
	}
	return TRUE;
}

void
x2_upload_url_clear(X2UploadUrl *url)
{
	if (url == NULL)
		return;
	g_free(url->host);
	g_free(url->path);
	memset(url, 0, sizeof(*url));
}

static void
header_free(gpointer data)
{
	X2UploadHeader *h = data;

	g_free(h->name);
	g_free(h->value);
	g_free(h);
}

void
x2_upload_slot_free(X2UploadSlot *slot)
{
	if (slot == NULL)
		return;
	g_free(slot->put);
	g_free(slot->get);
	x2_upload_url_clear(&slot->put_url);
	if (slot->headers != NULL)
		g_ptr_array_unref(slot->headers);
	g_free(slot);
}

static const char *
allowed_header_name(const char *name)
{
	int i;

	if (name == NULL)
		return NULL;
	for (i = 0; allowed_headers[i] != NULL; i++)
		if (g_ascii_strcasecmp(name, allowed_headers[i]) == 0)
			return allowed_headers[i];
	return NULL;
}

/* Vets <put url=/> or <get url=/>; *url gets the trimmed attribute. */
static gboolean
slot_url(const xmlnode *slot, const char *name, gboolean allow_http,
         char **url, X2UploadUrl *parsed, GError **error)
{
	xmlnode *node = xmlnode_get_child(slot, name);
	const char *attr = node ? xmlnode_get_attrib(node, "url") : NULL;
	char *copy;

	if (attr == NULL) {
		g_set_error(error, X2_UPLOAD_ERROR, X2_UPLOAD_ERROR_SLOT,
			"The upload slot has no <%s/> URL", name);
		return FALSE;
	}
	copy = g_strstrip(g_strdup(attr));
	if (!x2_upload_url_parse(copy, parsed, error)) {
		g_free(copy);
		return FALSE;
	}
	if (!parsed->https && !allow_http) {
		g_set_error(error, X2_UPLOAD_ERROR, X2_UPLOAD_ERROR_URL,
			"The server offered an unencrypted (http) %s URL; enable "
			"\"Allow unencrypted file upload\" in the account settings to "
			"use it", name);
		x2_upload_url_clear(parsed);
		g_free(copy);
		return FALSE;
	}
	*url = copy;
	return TRUE;
}

X2UploadSlot *
x2_upload_slot_parse(const xmlnode *slot, gboolean allow_http, GError **error)
{
	X2UploadSlot *s;
	X2UploadUrl get_url;
	xmlnode *put, *h;

	g_return_val_if_fail(slot != NULL, NULL);

	s = g_new0(X2UploadSlot, 1);
	s->headers = g_ptr_array_new_with_free_func(header_free);

	if (!slot_url(slot, "put", allow_http, &s->put, &s->put_url, error))
		goto fail;
	if (!slot_url(slot, "get", allow_http, &s->get, &get_url, error))
		goto fail;
	x2_upload_url_clear(&get_url);

	put = xmlnode_get_child(slot, "put");
	for (h = xmlnode_get_child(put, "header"); h; h = xmlnode_get_next_twin(h)) {
		const char *name = allowed_header_name(xmlnode_get_attrib(h, "name"));
		char *raw, *value;
		X2UploadHeader *hdr;

		if (name == NULL) {
			s->dropped++;
			continue;
		}
		raw = xmlnode_get_data(h);
		value = raw ? g_strstrip(g_strdup(raw)) : g_strdup("");
		g_free(raw);
		/* header injection: a CR, LF or other control char in a value */
		if (has_control_char(value, TRUE)) {
			g_set_error(error, X2_UPLOAD_ERROR, X2_UPLOAD_ERROR_HEADER,
				"The server sent a %s header containing line breaks or "
				"control characters", name);
			g_free(value);
			goto fail;
		}
		hdr = g_new0(X2UploadHeader, 1);
		hdr->name = g_strdup(name);
		hdr->value = value;
		g_ptr_array_add(s->headers, hdr);
	}
	return s;

fail:
	x2_upload_slot_free(s);
	return NULL;
}

char *
x2_upload_put_head(const X2UploadSlot *slot, guint64 size, const char *mime,
                   const char *user_agent)
{
	const X2UploadUrl *u;
	GString *head;
	guint i;

	g_return_val_if_fail(slot != NULL, NULL);
	u = &slot->put_url;
	if (mime == NULL || *mime == '\0' || has_control_char(mime, FALSE))
		mime = "application/octet-stream";

	head = g_string_new(NULL);
	g_string_append_printf(head, "PUT %s HTTP/1.1\r\n", u->path);
	if (u->port == (u->https ? 443 : 80))
		g_string_append_printf(head, "Host: %s\r\n", u->host);
	else
		g_string_append_printf(head, "Host: %s:%d\r\n", u->host, u->port);
	if (user_agent != NULL && !has_control_char(user_agent, FALSE))
		g_string_append_printf(head, "User-Agent: %s\r\n", user_agent);
	g_string_append_printf(head, "Content-Type: %s\r\n", mime);
	g_string_append_printf(head, "Content-Length: %" G_GUINT64_FORMAT "\r\n",
		size);
	for (i = 0; i < slot->headers->len; i++) {
		const X2UploadHeader *hdr = g_ptr_array_index(slot->headers, i);

		g_string_append_printf(head, "%s: %s\r\n", hdr->name, hdr->value);
	}
	g_string_append(head, "Connection: close\r\n\r\n");
	return g_string_free(head, FALSE);
}

gboolean
x2_upload_status_parse(const char *response, int *code)
{
	g_return_val_if_fail(code != NULL, FALSE);
	if (response == NULL || strncmp(response, "HTTP/1.", 7) != 0 ||
	    !g_ascii_isdigit(response[7]) || response[8] != ' ' ||
	    !g_ascii_isdigit(response[9]) || !g_ascii_isdigit(response[10]) ||
	    !g_ascii_isdigit(response[11]))
		return FALSE;
	*code = (response[9] - '0') * 100 + (response[10] - '0') * 10 +
	        (response[11] - '0');
	return TRUE;
}

xmlnode *
x2_upload_message_new(const char *to, gboolean groupchat, const char *url)
{
	xmlnode *message = xmlnode_new("message");
	xmlnode *body, *x, *u;

	xmlnode_set_attrib(message, "type", groupchat ? "groupchat" : "chat");
	xmlnode_set_attrib(message, "to", to);
	body = xmlnode_new_child(message, "body");
	xmlnode_insert_data(body, url, -1);
	x = xmlnode_new_child(message, "x");
	xmlnode_set_namespace(x, X2_NS_OOB_X);
	u = xmlnode_new_child(x, "url");
	xmlnode_insert_data(u, url, -1);
	return message;
}
