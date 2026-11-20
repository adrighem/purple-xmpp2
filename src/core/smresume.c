/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/* XEP-0198 resumption policy, see smresume.h. */
#include "smresume.h"

#include <string.h>

guint
x2_sm_resume_window(guint server_max)
{
	if (server_max == 0)
		return X2_SM_RESUME_DEFAULT_WINDOW_S;
	return MIN(server_max, X2_SM_RESUME_MAX_WINDOW_S);
}

gint
x2_sm_resume_lost(X2SmResume *r, gboolean resumable, guint server_max,
                  guint first_delay_ms, gint64 now)
{
	g_return_val_if_fail(r != NULL, -1);
	if (!resumable || r->phase != X2_SM_RESUME_IDLE)
		return -1;
	r->phase = X2_SM_RESUME_WAITING;
	r->attempts = 0;
	r->deadline = now + (gint64)x2_sm_resume_window(server_max)
	              * G_USEC_PER_SEC;
	return (gint)MIN(first_delay_ms, (guint)G_MAXINT);
}

gboolean
x2_sm_resume_start(X2SmResume *r, gint64 now)
{
	g_return_val_if_fail(r != NULL, FALSE);
	if (r->phase != X2_SM_RESUME_WAITING
	    || r->attempts >= X2_SM_RESUME_ATTEMPTS || now >= r->deadline) {
		x2_sm_resume_reset(r);
		return FALSE;
	}
	r->attempts++;
	r->phase = X2_SM_RESUME_ATTEMPTING;
	return TRUE;
}

gint
x2_sm_resume_failed(X2SmResume *r, gint64 now)
{
	g_return_val_if_fail(r != NULL, -1);
	if (r->phase != X2_SM_RESUME_ATTEMPTING
	    || r->attempts >= X2_SM_RESUME_ATTEMPTS
	    || now + (gint64)X2_SM_RESUME_RETRY_DELAY_MS * 1000 >= r->deadline) {
		x2_sm_resume_reset(r);
		return -1;
	}
	r->phase = X2_SM_RESUME_WAITING;
	return X2_SM_RESUME_RETRY_DELAY_MS;
}

void
x2_sm_resume_reset(X2SmResume *r)
{
	g_return_if_fail(r != NULL);
	r->phase = X2_SM_RESUME_IDLE;
	r->attempts = 0;
	r->deadline = 0;
}

gboolean
x2_sm_resume_reply_ok(gboolean resume_sent, gboolean post_auth)
{
	return resume_sent && post_auth;
}

char *
x2_sm_id_log(const char *id)
{
	if (id == NULL)
		return g_strdup("(none)");
	/* an id of four characters or fewer would be shown whole */
	if (strlen(id) <= 4)
		return g_strdup("...");
	return g_strdup_printf("%.4s...", id);
}
