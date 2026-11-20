/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */

/* Flag names, EV/debug filters and their matching. */
#include <stdlib.h>
#include <string.h>

#include "harness.h"

static const struct { const char *name; guint bit; } flag_names[] = {
	{ "SEND", PURPLE_MESSAGE_SEND }, { "RECV", PURPLE_MESSAGE_RECV },
	{ "SYSTEM", PURPLE_MESSAGE_SYSTEM }, { "AUTO_RESP", PURPLE_MESSAGE_AUTO_RESP },
	{ "ACTIVE_ONLY", PURPLE_MESSAGE_ACTIVE_ONLY }, { "NICK", PURPLE_MESSAGE_NICK },
	{ "NO_LOG", PURPLE_MESSAGE_NO_LOG }, { "WHISPER", PURPLE_MESSAGE_WHISPER },
	{ "ERROR", PURPLE_MESSAGE_ERROR }, { "DELAYED", PURPLE_MESSAGE_DELAYED },
	{ "RAW", PURPLE_MESSAGE_RAW }, { "IMAGES", PURPLE_MESSAGE_IMAGES },
	{ "NOTIFY", PURPLE_MESSAGE_NOTIFY }, { "NO_LINKIFY", PURPLE_MESSAGE_NO_LINKIFY },
	{ "INVISIBLE", PURPLE_MESSAGE_INVISIBLE },
	{ "REMOTE_SEND", PURPLE_MESSAGE_REMOTE_SEND },
};

static guint parse_flags(const char *spec, gboolean *ok)
{
	char **parts = g_strsplit_set(spec, "|,", -1);
	guint bits = 0;
	int i;
	gsize k;

	for (i = 0; parts[i]; i++) {
		gboolean found = FALSE;

		for (k = 0; k < G_N_ELEMENTS(flag_names); k++) {
			if (!strcmp(parts[i], flag_names[k].name)) {
				bits |= flag_names[k].bit;
				found = TRUE;
			}
		}
		if (!found && *parts[i])
			*ok = FALSE;
	}
	g_strfreev(parts);
	return bits;
}

char *flag_list(guint flags)
{
	GString *out = g_string_new(NULL);
	gsize k;

	for (k = 0; k < G_N_ELEMENTS(flag_names); k++) {
		if (flags & flag_names[k].bit)
			g_string_append_printf(out, "%s%s", out->len ? "," : "", flag_names[k].name);
	}
	if (!out->len)
		g_string_append_c(out, '-');
	return g_string_free(out, FALSE);
}

void filter_free(Filter *f)
{
	if (!f)
		return;
	g_free(f->kind); g_free(f->conv); g_free(f->who); g_free(f->ctype);
	g_free(f->cat); g_free(f->text); g_free(f->texteq);
	g_free(f);
}

/* Parse "k=v k=v ... [text=REST]"; NULL and *err on a bad token. */
Filter *filter_parse(const char *args, double default_timeout, char **err)
{
	Filter *f = g_new0(Filter, 1);
	const char *p = args;
	gboolean ok = TRUE;

	f->timeout = default_timeout;
	f->n = -1;
	while (*p) {
		const char *end;
		char *tok, *eq;

		while (*p == ' ')
			p++;
		if (!*p)
			break;
		if (g_str_has_prefix(p, "text=")) {
			f->text = g_strdup(p + 5);
			break;
		}
		if (g_str_has_prefix(p, "texteq=")) {
			f->texteq = g_strdup(p + 7);
			break;
		}
		end = strchr(p, ' ');
		tok = end ? g_strndup(p, end - p) : g_strdup(p);
		p = end ? end : p + strlen(p);
		eq = strchr(tok, '=');
		if (!eq) {
			*err = g_strdup_printf("bad filter token '%s'", tok);
			g_free(tok);
			filter_free(f);
			return NULL;
		}
		*eq++ = '\0';
		if (!strcmp(tok, "kind")) f->kind = g_strdup(eq);
		else if (!strcmp(tok, "conv")) f->conv = g_strdup(eq);
		else if (!strcmp(tok, "who")) f->who = g_strdup(eq);
		else if (!strcmp(tok, "ctype")) f->ctype = g_strdup(eq);
		else if (!strcmp(tok, "cat")) f->cat = g_strdup(eq);
		else if (!strcmp(tok, "flag")) f->flag_req = parse_flags(eq, &ok);
		else if (!strcmp(tok, "noflag")) f->flag_no = parse_flags(eq, &ok);
		else if (!strcmp(tok, "timeout")) f->timeout = atof(eq);
		else if (!strcmp(tok, "n")) f->n = atoi(eq);
		else if (!strcmp(tok, "mtime")) {
			f->mt_epoch = (time_t)atoll(eq);
			f->mt_secs = strchr(eq, ',') ? atoi(strchr(eq, ',') + 1) : 2;
		} else {
			*err = g_strdup_printf("unknown filter key '%s'", tok);
			ok = FALSE;
		}
		g_free(tok);
		if (!ok) {
			if (!*err)
				*err = g_strdup("unknown flag name");
			filter_free(f);
			return NULL;
		}
	}
	return f;
}

/* A filter without a resource matches the conversation name with or without one
 * (libpurple may name an IM conversation after the sender's full JID). */
static gboolean conv_matches(const char *want, const char *have)
{
	const char *slash = strchr(have, '/');

	if (!strcmp(want, have))
		return TRUE;
	return !strchr(want, '/') && slash && strlen(want) == (gsize)(slash - have) &&
	       !strncmp(want, have, slash - have);
}

static gboolean ev_matches(const Filter *f, const Ev *e)
{
	if (f->kind && strcmp(f->kind, e->kind))
		return FALSE;
	if (f->conv && !conv_matches(f->conv, e->conv))
		return FALSE;
	if (f->who && !g_str_has_prefix(e->who, f->who))
		return FALSE;
	if (f->ctype && strcmp(f->ctype, e->chat ? "chat" : "im"))
		return FALSE;
	if ((e->flags & f->flag_req) != f->flag_req || (e->flags & f->flag_no))
		return FALSE;
	if (f->mt_epoch && labs((long)(e->mtime - f->mt_epoch)) > f->mt_secs)
		return FALSE;
	if (f->text && !strstr(e->text, f->text))
		return FALSE;
	if (f->texteq && strcmp(e->text, f->texteq))
		return FALSE;
	return TRUE;
}

static gboolean dbg_matches(const Filter *f, const Dbg *d)
{
	if (f->cat && !g_str_has_prefix(d->cat, f->cat))
		return FALSE;
	if (f->text && !strstr(d->text, f->text))
		return FALSE;
	if (f->texteq && strcmp(d->text, f->texteq))
		return FALSE;
	return TRUE;
}

int ev_find(const Filter *f, guint from)
{
	guint i;

	for (i = from; i < evs->len; i++)
		if (ev_matches(f, g_ptr_array_index(evs, i)))
			return (int)i;
	return -1;
}

int dbg_find(const Filter *f, guint from)
{
	guint i;

	for (i = from; i < dbgs->len; i++)
		if (dbg_matches(f, g_ptr_array_index(dbgs, i)))
			return (int)i;
	return -1;
}

int ev_count(const Filter *f)
{
	guint i;
	int n = 0;

	for (i = 0; i < evs->len; i++)
		n += ev_matches(f, g_ptr_array_index(evs, i));
	return n;
}

int dbg_count(const Filter *f)
{
	guint i;
	int n = 0;

	for (i = 0; i < dbgs->len; i++)
		n += dbg_matches(f, g_ptr_array_index(dbgs, i));
	return n;
}
