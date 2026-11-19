/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file corrlog.c Who wrote which message id (XEP-0308 sender check)
 */
#include "corrlog.h"

struct _X2CorrLog {
	guint capacity;
	GHashTable *authors;   /* "peer\nid" -> sender */
	GQueue order;          /* the keys, oldest at the head */
	GHashTable *last_sent; /* peer -> id */
};

X2CorrLog *
x2_corrlog_new(guint capacity)
{
	X2CorrLog *log;

	g_return_val_if_fail(capacity > 0, NULL);

	log = g_new0(X2CorrLog, 1);
	log->capacity = capacity;
	log->authors = g_hash_table_new_full(g_str_hash, g_str_equal,
	                                     g_free, g_free);
	log->last_sent = g_hash_table_new_full(g_str_hash, g_str_equal,
	                                       g_free, g_free);
	g_queue_init(&log->order);
	return log;
}

void
x2_corrlog_free(X2CorrLog *log)
{
	if (log == NULL)
		return;
	/* the queue holds the keys of the table, which frees them */
	g_hash_table_destroy(log->authors);
	g_hash_table_destroy(log->last_sent);
	g_queue_clear(&log->order);
	g_free(log);
}

static char *
make_key(const char *peer, const char *id)
{
	return g_strconcat(peer, "\n", id, NULL);
}

void
x2_corrlog_record(X2CorrLog *log, const char *peer, const char *id,
                  const char *sender)
{
	char *key;

	g_return_if_fail(log != NULL);
	g_return_if_fail(peer != NULL && id != NULL && sender != NULL);

	key = make_key(peer, id);
	if (g_hash_table_contains(log->authors, key)) {
		/* same id again: the first author stands, a later forger
		 * cannot take over an id */
		g_free(key);
		return;
	}
	if (g_hash_table_size(log->authors) >= log->capacity) {
		char *oldest = g_queue_pop_head(&log->order);

		g_hash_table_remove(log->authors, oldest);
	}
	g_hash_table_insert(log->authors, key, g_strdup(sender));
	g_queue_push_tail(&log->order, key);
}

X2CorrVerdict
x2_corrlog_check(const X2CorrLog *log, const char *peer, const char *id,
                 const char *sender)
{
	char *key;
	const char *author;

	g_return_val_if_fail(log != NULL, X2_CORR_UNKNOWN);
	g_return_val_if_fail(peer != NULL && id != NULL && sender != NULL,
	                     X2_CORR_UNKNOWN);

	key = make_key(peer, id);
	author = g_hash_table_lookup(log->authors, key);
	g_free(key);
	if (author == NULL)
		return X2_CORR_UNKNOWN;
	return g_str_equal(author, sender) ? X2_CORR_OK : X2_CORR_MISMATCH;
}

void
x2_corrlog_set_last_sent(X2CorrLog *log, const char *peer, const char *id)
{
	g_return_if_fail(log != NULL);
	g_return_if_fail(peer != NULL && id != NULL);

	/* one short string per conversation, but do not grow without bound */
	if (g_hash_table_size(log->last_sent) >= log->capacity)
		g_hash_table_remove_all(log->last_sent);
	g_hash_table_insert(log->last_sent, g_strdup(peer), g_strdup(id));
}

const char *
x2_corrlog_last_sent(const X2CorrLog *log, const char *peer)
{
	g_return_val_if_fail(log != NULL && peer != NULL, NULL);

	return g_hash_table_lookup(log->last_sent, peer);
}
