/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */

/*
 * File transfer UI, memory and proxy support of xmpp2-harness, for the
 * streaming upload tests.
 *
 * File transfer UI ops: every progress update the prpl reports through
 * purple_xfer_update_progress() is printed as
 *   EV xfer-progress bytes=N size=M
 * so a test can check that progress is reported, grows and ends at the size.
 *
 * Commands: xfer-cancel-at, rss-reset, rss-check (see the table in
 * harness_script.c). Option --proxy HOST:PORT: the account uses this HTTP
 * (CONNECT) proxy for everything, c2s and file upload alike.
 */
#include <stdlib.h>
#include <string.h>

#include "harness.h"

static guint64 xfer_cancel_at;      /* 0 = no cancel armed */
static gboolean xfer_cancel_queued;
static char xfer_cancel_how[16] = "cancel";
static long rss_base_kb;

/* kB value of a /proc/self/status field, -1 if unreadable */
static long proc_status_kb(const char *field)
{
	char *text = NULL, **lines, **l;
	long kb = -1;
	size_t flen = strlen(field);

	if (!g_file_get_contents("/proc/self/status", &text, NULL, NULL))
		return -1;
	lines = g_strsplit(text, "\n", -1);
	for (l = lines; *l; l++) {
		if (!strncmp(*l, field, flen) && (*l)[flen] == ':') {
			kb = atol(*l + flen + 1);
			break;
		}
	}
	g_strfreev(lines);
	g_free(text);
	return kb;
}

static gboolean xfer_cancel_idle(gpointer data)
{
	PurpleXfer *xfer = data;

	xfer_cancel_queued = FALSE;
	/* it may have ended meanwhile */
	if (!g_list_find(purple_xfers_get_all(), xfer))
		return FALSE;
	if (!strcmp(xfer_cancel_how, "disconnect")) {
		purple_account_set_enabled(account, UI_ID, FALSE);
	} else if (!strcmp(xfer_cancel_how, "remove")) {
		purple_accounts_delete(account);
		account = NULL;
	} else {
		purple_xfer_cancel_local(xfer);
	}
	return FALSE;
}

static void xfer_ui_update_progress(PurpleXfer *xfer, double percent)
{
	guint64 sent = purple_xfer_get_bytes_sent(xfer);

	fprintf(out, "EV xfer-progress bytes=%" G_GUINT64_FORMAT " size=%" G_GUINT64_FORMAT "\n",
	        sent, (guint64)purple_xfer_get_size(xfer));
	fflush(out);
	if (xfer_cancel_at && !xfer_cancel_queued && sent >= xfer_cancel_at) {
		xfer_cancel_queued = TRUE;
		xfer_cancel_at = 0;
		g_idle_add(xfer_cancel_idle, xfer);
	}
}

static PurpleXferUiOps xfer_ui_ops = {
	.update_progress = xfer_ui_update_progress
};

void harness_xfer_init(const char *proxy)
{
	purple_xfers_set_ui_ops(&xfer_ui_ops);
	if (proxy) {
		char *spec = g_strdup(proxy);
		char *colon = strrchr(spec, ':');
		PurpleProxyInfo *info = purple_proxy_info_new();

		if (!colon) {
			fprintf(stderr, "--proxy wants HOST:PORT\n");
			exit(2);
		}
		*colon = '\0';
		purple_proxy_info_set_type(info, PURPLE_PROXY_HTTP);
		purple_proxy_info_set_host(info, spec);
		purple_proxy_info_set_port(info, atoi(colon + 1));
		purple_account_set_proxy_info(account, info);
		g_free(spec);
	}
}

/* "xfer-cancel-at BYTES [cancel|disconnect|remove]" */
void cmd_xfer_cancel_at(const char *cmd, char *arg)
{
	char *how = strchr(arg, ' ');

	if (how)
		*how++ = '\0';
	g_strlcpy(xfer_cancel_how, how && *how ? how : "cancel", sizeof(xfer_cancel_how));
	if (strcmp(xfer_cancel_how, "cancel") && strcmp(xfer_cancel_how, "disconnect") &&
	    strcmp(xfer_cancel_how, "remove")) {
		fail_now(cmd, "usage: xfer-cancel-at BYTES [cancel|disconnect|remove]");
		return;
	}
	xfer_cancel_at = g_ascii_strtoull(arg, NULL, 10);
	xfer_cancel_queued = FALSE;
	continue_ok(cmd, arg);
}

/* "rss-reset": reset the peak RSS (VmHWM) and remember the current RSS */
void cmd_rss_reset(const char *cmd, char *arg)
{
	FILE *fp = fopen("/proc/self/clear_refs", "w");

	/* 5: reset the peak RSS (Linux 4.0 and later) */
	if (!fp || fputs("5", fp) == EOF || fclose(fp) == EOF) {
		fail_now(cmd, "cannot reset VmHWM through /proc/self/clear_refs");
		return;
	}
	rss_base_kb = proc_status_kb("VmRSS");
	continue_ok(cmd, NULL);
}

/* "rss-check DELTA_MB": FAIL if VmHWM grew by more than that since rss-reset */
void cmd_rss_check(const char *cmd, char *arg)
{
	long hwm = proc_status_kb("VmHWM");
	long limit = atol(arg) * 1024;
	char *m;

	if (hwm < 0 || rss_base_kb <= 0) {
		fail_now(cmd, "no rss-reset before, or /proc/self/status unreadable");
		return;
	}
	m = g_strdup_printf("peak grew by %ld kB (limit %ld kB)", hwm - rss_base_kb, limit);
	if (hwm - rss_base_kb > limit)
		fail_now(cmd, m);
	else
		continue_ok(cmd, m);
	g_free(m);
}
