/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file x2stream.h Per-stream state of the xmpp2 feature modules
 *
 * JabberStream::x2 points to one of these.  Shared state (dedup, trusted
 * service JIDs) lives in named fields owned by ext.c.  Each feature module
 * gets one opaque slot in priv[], indexed by its position in the registry
 * table, so adding a module never touches this header: use
 * x2_ext_get_priv() / x2_ext_set_priv() from ext.h, allocate in the
 * stream_new hook and free (and reset to NULL) in stream_free.
 */
#ifndef XMPP2_EXT_X2STREAM_H_
#define XMPP2_EXT_X2STREAM_H_

#include <glib.h>

#include "core/dedup.h"
#include "ext/x2types.h"

/** Capacity of the registry; ext.c checks that the table fits. */
#define X2_EXT_MAX 16

struct _X2Stream {
	/* Keys of recently seen and sent messages, see core/dedup.h */
	X2Dedup *dedup;

	/* JIDs listed in our own server's disco#items (set of char *) */
	GHashTable *server_items;

	/* Archives (own bare JID, rooms) that advertised urn:xmpp:sid:0 or
	 * urn:xmpp:mam:2: only their stanza-ids are trusted (set of char *) */
	GHashTable *trusted_archives;

	/* One slot per registered module, see x2_ext_get_priv() */
	gpointer priv[X2_EXT_MAX];
};

#endif /* XMPP2_EXT_X2STREAM_H_ */
