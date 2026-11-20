/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file jid.h Bare-JID helpers shared by the core modules (pure logic)
 *
 * Only <glib.h> is used.  The bare part of a JID is everything before the
 * first '/': a resource may itself contain slashes, and the node and domain
 * cannot.  These helpers do not validate or normalise anything.
 */
#ifndef XMPP2_CORE_JID_H_
#define XMPP2_CORE_JID_H_

#include <glib.h>

/** Length of the bare part of @a jid (up to the first '/'). */
gsize x2_jid_bare_len(const char *jid);

/**
 * The bare part of @a jid.
 *
 * @return A newly allocated string, or NULL for NULL or empty input.
 */
char *x2_jid_bare(const char *jid);

/** TRUE if the bare part of @a jid equals @a bare exactly (NULL: FALSE). */
gboolean x2_jid_bare_equal(const char *jid, const char *bare);

/**
 * TRUE if the bare parts of @a a and @a b are equal, ASCII case-insensitive
 * (NULL: FALSE).  The resources are ignored.
 */
gboolean x2_jid_same_bare(const char *a, const char *b);

#endif /* XMPP2_CORE_JID_H_ */
