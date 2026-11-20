/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */

/* File transfer commands: upload, upload-fail, upload-drop. */
#include <stdlib.h>
#include <string.h>

#include "harness.h"

/* "upload JID PATH [SECS]"; the -fail and -drop variants expect a cancel. */
void cmd_upload(const char *cmd, char *arg)
{
	char *rest = split_word(arg);
	char *sp = strchr(rest, ' ');
	double secs = 30;

	if (sp) {
		*sp = '\0';
		secs = atof(sp + 1);
	}
	if (!*rest || !need_online(cmd))
		return;
	pending_fail_ok = strcmp(cmd, "upload") != 0;
	arm(P_XFER, cmd, secs);
	serv_send_file(purple_account_get_connection(account), arg, rest);
	if (!strcmp(cmd, "upload-drop") && drop_c2s_sockets() == 0)
		resume(FALSE, "no socket connected to the c2s port");
}
