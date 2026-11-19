/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file dedup.c Bounded LRU set of message keys (pure logic)
 */
#include "dedup.h"

struct _X2Dedup {
	guint capacity;
	GHashTable *index;  /* key (owned by the queue element) -> GList link */
	GQueue order;       /* head = most recently used, tail = least */
};

X2Dedup *
x2_dedup_new(guint capacity)
{
	X2Dedup *d;

	g_return_val_if_fail(capacity > 0, NULL);

	d = g_new0(X2Dedup, 1);
	d->capacity = capacity;
	d->index = g_hash_table_new(g_str_hash, g_str_equal);
	g_queue_init(&d->order);
	return d;
}

void
x2_dedup_free(X2Dedup *d)
{
	if (d == NULL)
		return;
	g_queue_clear_full(&d->order, g_free);
	g_hash_table_destroy(d->index);
	g_free(d);
}

guint
x2_dedup_size(const X2Dedup *d)
{
	g_return_val_if_fail(d != NULL, 0);

	return g_queue_get_length((GQueue *) &d->order);
}

gboolean
x2_dedup_contains(const X2Dedup *d, const char *key)
{
	g_return_val_if_fail(d != NULL, FALSE);
	g_return_val_if_fail(key != NULL, FALSE);

	return g_hash_table_contains(d->index, key);
}

gboolean
x2_dedup_add(X2Dedup *d, const char *key)
{
	GList *link;

	g_return_val_if_fail(d != NULL, FALSE);
	g_return_val_if_fail(key != NULL, FALSE);

	link = g_hash_table_lookup(d->index, key);
	if (link != NULL) {
		g_queue_unlink(&d->order, link);
		g_queue_push_head_link(&d->order, link);
		return FALSE;
	}

	if (g_queue_get_length(&d->order) >= d->capacity) {
		char *old = g_queue_pop_tail(&d->order);

		g_hash_table_remove(d->index, old);
		g_free(old);
	}

	g_queue_push_head(&d->order, g_strdup(key));
	g_hash_table_insert(d->index, d->order.head->data, d->order.head);
	return TRUE;
}

char *
x2_dedup_make_key(const char *trusted_stanza_id, gboolean outgoing,
                  const char *peer, const char *origin_id)
{
	const char *dir = outgoing ? "out" : "in";

	if (trusted_stanza_id != NULL && *trusted_stanza_id != '\0')
		return g_strconcat("S:", trusted_stanza_id, NULL);
	if (peer == NULL || *peer == '\0')
		return NULL;
	if (origin_id != NULL && *origin_id != '\0')
		return g_strconcat("O:", dir, ":", peer, ":", origin_id, NULL);
	return NULL;
}
