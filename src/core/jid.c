/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file jid.c Bare-JID helpers shared by the core modules (pure logic)
 */
#include <string.h>

#include "jid.h"

gsize
x2_jid_bare_len(const char *jid)
{
	const char *slash;

	g_return_val_if_fail(jid != NULL, 0);

	slash = strchr(jid, '/');
	return slash != NULL ? (gsize)(slash - jid) : strlen(jid);
}

char *
x2_jid_bare(const char *jid)
{
	if (jid == NULL || *jid == '\0')
		return NULL;
	return g_strndup(jid, x2_jid_bare_len(jid));
}

gboolean
x2_jid_bare_equal(const char *jid, const char *bare)
{
	gsize len;

	if (jid == NULL || bare == NULL)
		return FALSE;
	len = x2_jid_bare_len(jid);
	return len == strlen(bare) && strncmp(jid, bare, len) == 0;
}

gboolean
x2_jid_same_bare(const char *a, const char *b)
{
	gsize len;

	if (a == NULL || b == NULL)
		return FALSE;
	len = x2_jid_bare_len(a);
	return len == x2_jid_bare_len(b) && g_ascii_strncasecmp(a, b, len) == 0;
}
