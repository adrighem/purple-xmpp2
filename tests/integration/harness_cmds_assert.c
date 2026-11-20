/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */

/* Assertion commands: EV and debug expectations, logcount, legacy aliases. */
#include <stdlib.h>
#include <string.h>

#include "harness.h"

static int logcount(const char *userdir, const char *jid, const char *text);

void cmd_clear_ev(const char *cmd, char *arg)
{
	g_ptr_array_set_size(evs, 0);
	ev_cursor = 0;
	continue_ok("clear-ev", NULL);
}

void cmd_clear_debug(const char *cmd, char *arg)
{
	g_ptr_array_set_size(dbgs, 0);
	dbg_cursor = 0;
	continue_ok("clear-debug", NULL);
}

void cmd_dump_debug(const char *cmd, char *arg)
{
	guint i;

	for (i = 0; i < dbgs->len; i++) {
		Dbg *d = g_ptr_array_index(dbgs, i);

		fprintf(out, "DBG %s: %s%s", d->cat, d->text, g_str_has_suffix(d->text, "\n") ? "" : "\n");
	}
	continue_ok("dump-debug", NULL);
}

void cmd_logcount(const char *cmd, char *arg)
{
	char *text = split_word(arg);
	char *sp = strrchr(text, ' ');
	int want, got;

	if (!sp || !userdir_path) {
		fail_now("logcount", "usage 'logcount JID TEXT N' (needs --userdir)");
		return;
	}
	want = atoi(sp + 1);
	*sp = '\0';
	got = logcount(userdir_path, arg, text);
	if (got == want) {
		continue_ok("logcount", NULL);
	} else {
		char *m = g_strdup_printf("'%s' in logs of %s: want %d got %d", text, arg, want, got);

		fail_now("logcount", m);
		g_free(m);
	}
}

static int logcount_dir(const char *dir, const char *text)
{
	GDir *d = g_dir_open(dir, 0, NULL);
	const char *name;
	int n = 0;

	if (!d)
		return 0;
	while ((name = g_dir_read_name(d))) {
		char *path = g_build_filename(dir, name, NULL), *data = NULL;
		const char *p;

		if (g_file_get_contents(path, &data, NULL, NULL)) {
			for (p = strstr(data, text); p; p = strstr(p + 1, text))
				n++;
			g_free(data);
		}
		g_free(path);
	}
	g_dir_close(d);
	return n;
}

/* logs/PROTO/ACCOUNT/ESCAPED_JID/DATE.txt */
static int logcount(const char *userdir, const char *jid, const char *text)
{
	char *root = g_build_filename(userdir, "logs", NULL);
	char *esc = g_strdup(purple_escape_filename(jid));
	GDir *protos = g_dir_open(root, 0, NULL);
	const char *pn;
	int n = 0;

	while (protos && (pn = g_dir_read_name(protos))) {
		char *pdir = g_build_filename(root, pn, NULL);
		GDir *accts = g_dir_open(pdir, 0, NULL);
		const char *an;

		while (accts && (an = g_dir_read_name(accts))) {
			char *cdir = g_build_filename(pdir, an, esc, NULL);

			n += logcount_dir(cdir, text);
			g_free(cdir);
		}
		if (accts)
			g_dir_close(accts);
		g_free(pdir);
	}
	if (protos)
		g_dir_close(protos);
	g_free(root);
	g_free(esc);
	return n;
}

void cmd_assert(const char *cmd, char *arg)
{
	gboolean dbg = g_str_has_suffix(cmd, "debug") || g_str_has_suffix(cmd, "debug-next");
	gboolean absent = g_str_has_prefix(cmd, "no-");
	gboolean count = g_str_has_prefix(cmd, "count-");
	gboolean next = g_str_has_suffix(cmd, "-next");
	char *err = NULL;
	Filter *f = filter_parse(arg, absent ? 3 : 20, &err);
	int got;

	if (!f) {
		fail_now(cmd, err);
		g_free(err);
		return;
	}
	if (dbg && f->cat && strncmp(f->cat, "xmpp2", 5) != 0) {
		fail_now(cmd, "only xmpp2* debug categories may be asserted");
		filter_free(f);
		return;
	}
	if (dbg && !f->cat)
		f->cat = g_strdup("xmpp2");
	if (count) {
		got = dbg ? dbg_count(f) : ev_count(f);
		if (f->n < 0) {
			fail_now(cmd, "count needs n=N");
		} else if (got != f->n) {
			char *m = g_strdup_printf("want %d got %d", f->n, got);

			fail_now(cmd, m);
			g_free(m);
		} else {
			continue_ok(cmd, NULL);
		}
		filter_free(f);
		return;
	}
	pending_filter = f;
	pending_next = next;
	arm(absent ? (dbg ? P_ABSENT_DBG : P_ABSENT_EV) : (dbg ? P_EXPECT_DBG : P_EXPECT_EV), cmd,
	    f->timeout);
	poll_pending();
}

void cmd_legacy(const char *cmd, char *arg)
{
	gboolean w = !strcmp(cmd, "wait");
	char *sp = strrchr(arg, ' ');
	double secs = w ? 20 : 3;
	char *line;

	if (!strcmp(cmd, "count")) {
		if (!sp) {
			fail_now(cmd, "usage 'count TEXT N'");
			return;
		}
		line = g_strdup_printf("kind=recv n=%d text=%.*s", atoi(sp + 1), (int)(sp - arg), arg);
		cmd_assert("count-ev", line);
		g_free(line);
		return;
	}
	if (sp && g_ascii_isdigit(sp[1]) && atof(sp + 1) > 0) {
		secs = atof(sp + 1);
		*sp = '\0';
	}
	line = g_strdup_printf("kind=recv timeout=%g text=%s", secs, arg);
	cmd_assert(w ? "expect-ev" : "no-ev", line);
	g_free(line);
}
