/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file store.c Persistent archive state: cursors and key rings (pure logic)
 */
#include <errno.h>
#include <string.h>

#include "store.h"

#define GROUP_PREFIX "archive "
#define STORE_VERSION 1

typedef struct {
	char *cursor;
	gint64 cursor_ts;
	GQueue ring;           /* of char *, head = oldest */
	GHashTable *ring_set;  /* set of keys, borrowed from ring */
} Archive;

struct _X2Store {
	GHashTable *archives;  /* bare JID -> Archive */
	gboolean cursor_only;
	gboolean dirty;
};

static void
archive_free(gpointer p)
{
	Archive *a = p;

	g_free(a->cursor);
	g_hash_table_destroy(a->ring_set);
	g_queue_clear_full(&a->ring, g_free);
	g_free(a);
}

static Archive *
archive_get(X2Store *s, const char *jid)
{
	Archive *a = g_hash_table_lookup(s->archives, jid);

	if (a == NULL) {
		a = g_new0(Archive, 1);
		g_queue_init(&a->ring);
		a->ring_set = g_hash_table_new(g_str_hash, g_str_equal);
		g_hash_table_insert(s->archives, g_strdup(jid), a);
	}
	return a;
}

X2Store *
x2_store_new(void)
{
	X2Store *s = g_new0(X2Store, 1);

	s->archives = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
	                                    archive_free);
	return s;
}

void
x2_store_free(X2Store *s)
{
	if (s == NULL)
		return;
	g_hash_table_destroy(s->archives);
	g_free(s);
}

/* The ring holds each key once; the set borrows the strings of the queue. */
static gboolean
ring_push(Archive *a, const char *key)
{
	char *k;

	if (g_hash_table_contains(a->ring_set, key))
		return FALSE;
	k = g_strdup(key);
	g_queue_push_tail(&a->ring, k);
	g_hash_table_add(a->ring_set, k);
	while (g_queue_get_length(&a->ring) > X2_STORE_RING_MAX) {
		char *old = g_queue_pop_head(&a->ring);

		g_hash_table_remove(a->ring_set, old);
		g_free(old);
	}
	return TRUE;
}

static void
load_archive(X2Store *s, GKeyFile *kf, const char *group)
{
	Archive *a = archive_get(s, group + strlen(GROUP_PREFIX));
	char **ring;
	gsize i, n = 0;

	a->cursor = g_key_file_get_string(kf, group, "cursor", NULL);
	if (a->cursor != NULL && *a->cursor == '\0') {
		g_free(a->cursor);
		a->cursor = NULL;
	}
	a->cursor_ts = g_key_file_get_int64(kf, group, "cursor_ts", NULL);
	ring = g_key_file_get_string_list(kf, group, "ring", &n, NULL);
	for (i = 0; ring != NULL && i < n; i++)
		if (*ring[i] != '\0')
			ring_push(a, ring[i]);
	g_strfreev(ring);
}

X2Store *
x2_store_load(const char *path)
{
	X2Store *s = x2_store_new();
	GKeyFile *kf;
	GError *err = NULL;
	char **groups;
	char *initial;
	gsize i;

	g_return_val_if_fail(path != NULL, s);

	kf = g_key_file_new();
	if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &err)) {
		if (!g_error_matches(err, G_FILE_ERROR, G_FILE_ERROR_NOENT))
			g_warning("xmpp2: state file %s unreadable, starting "
			          "fresh: %s", path, err->message);
		g_error_free(err);
		g_key_file_free(kf);
		return s;
	}

	groups = g_key_file_get_groups(kf, NULL);
	for (i = 0; groups[i] != NULL; i++)
		if (g_str_has_prefix(groups[i], GROUP_PREFIX) &&
		    groups[i][strlen(GROUP_PREFIX)] != '\0')
			load_archive(s, kf, groups[i]);
	g_strfreev(groups);

	initial = g_key_file_get_string(kf, "meta", "initial", NULL);
	s->cursor_only = g_strcmp0(initial, "cursor-only") == 0;
	g_free(initial);

	g_key_file_free(kf);
	return s;
}

char *
x2_store_to_data(const X2Store *s, gsize *len)
{
	GKeyFile *kf;
	GHashTableIter it;
	gpointer k, v;
	char *data;

	g_return_val_if_fail(s != NULL, NULL);

	kf = g_key_file_new();
	g_hash_table_iter_init(&it, s->archives);
	while (g_hash_table_iter_next(&it, &k, &v)) {
		Archive *a = v;
		char *group = g_strconcat(GROUP_PREFIX, (char *)k, NULL);

		if (a->cursor != NULL)
			g_key_file_set_string(kf, group, "cursor", a->cursor);
		if (a->cursor_ts != 0)
			g_key_file_set_int64(kf, group, "cursor_ts", a->cursor_ts);
		if (!g_queue_is_empty(&a->ring)) {
			const char **list = g_new0(const char *,
			                           g_queue_get_length(&a->ring) + 1);
			GList *l;
			gsize i = 0;

			for (l = a->ring.head; l; l = l->next)
				list[i++] = l->data;
			g_key_file_set_string_list(kf, group, "ring", list, i);
			g_free(list);
		}
		g_free(group);
	}
	if (s->cursor_only)
		g_key_file_set_string(kf, "meta", "initial", "cursor-only");
	g_key_file_set_integer(kf, "meta", "version", STORE_VERSION);

	data = g_key_file_to_data(kf, len, NULL);
	g_key_file_free(kf);
	return data;
}

gboolean
x2_store_save(X2Store *s, const char *path, GError **error)
{
	char *data;
	gsize len;
	gboolean ok;

	g_return_val_if_fail(s != NULL, FALSE);
	g_return_val_if_fail(path != NULL, FALSE);

	data = x2_store_to_data(s, &len);
	ok = g_file_set_contents_full(path, data, len,
	                              G_FILE_SET_CONTENTS_CONSISTENT, 0600,
	                              error);
	g_free(data);
	if (ok)
		s->dirty = FALSE;
	return ok;
}

gboolean
x2_store_is_dirty(const X2Store *s)
{
	g_return_val_if_fail(s != NULL, FALSE);
	return s->dirty;
}

const char *
x2_store_get_cursor(const X2Store *s, const char *archive, gint64 *ts)
{
	Archive *a;

	g_return_val_if_fail(s != NULL && archive != NULL, NULL);

	a = g_hash_table_lookup(s->archives, archive);
	if (ts != NULL)
		*ts = a ? a->cursor_ts : 0;
	return a ? a->cursor : NULL;
}

void
x2_store_set_cursor(X2Store *s, const char *archive, const char *id,
                    gint64 ts)
{
	Archive *a;

	g_return_if_fail(s != NULL && archive != NULL);
	g_return_if_fail(id != NULL && *id != '\0');

	a = archive_get(s, archive);
	if (g_strcmp0(a->cursor, id) != 0) {
		g_free(a->cursor);
		a->cursor = g_strdup(id);
		s->dirty = TRUE;
	}
	if (ts != 0 && ts != a->cursor_ts) {
		a->cursor_ts = ts;
		s->dirty = TRUE;
	}
}

void
x2_store_ring_add(X2Store *s, const char *archive, const char *key)
{
	g_return_if_fail(s != NULL && archive != NULL);
	g_return_if_fail(key != NULL && *key != '\0');

	if (ring_push(archive_get(s, archive), key))
		s->dirty = TRUE;
}

gboolean
x2_store_ring_contains(const X2Store *s, const char *archive,
                       const char *key)
{
	Archive *a;

	g_return_val_if_fail(s != NULL && archive != NULL, FALSE);

	a = g_hash_table_lookup(s->archives, archive);
	return a != NULL && key != NULL &&
	       g_hash_table_contains(a->ring_set, key);
}

void
x2_store_ring_foreach(const X2Store *s, GFunc fn, gpointer data)
{
	GHashTableIter it;
	gpointer v;

	g_return_if_fail(s != NULL && fn != NULL);

	g_hash_table_iter_init(&it, s->archives);
	while (g_hash_table_iter_next(&it, NULL, &v))
		g_queue_foreach(&((Archive *)v)->ring, fn, data);
}

gboolean
x2_store_cursor_only(const X2Store *s)
{
	g_return_val_if_fail(s != NULL, FALSE);
	return s->cursor_only;
}

void
x2_store_set_cursor_only(X2Store *s, gboolean on)
{
	g_return_if_fail(s != NULL);
	if (s->cursor_only != on) {
		s->cursor_only = on;
		s->dirty = TRUE;
	}
}
