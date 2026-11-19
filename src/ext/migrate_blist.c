/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file migrate_blist.c Copy of the buddy list entries of one account
 *
 * Buddies are only added locally: the roster lives on the server and is
 * synchronised at the clone's first login, so nothing is sent for them
 * (purple_account_add_buddy() is not called).  What is kept here is what
 * the server does not know: groups, local aliases, contact grouping, room
 * bookmarks and their auto-join flag.
 */
#include "xmpp2_internal.h"

#include "blist.h"
#include "debug.h"

#include "ext/migrate.h"

#define DBG X2_MIGRATE_DEBUG

/* Pidgin's blist node settings of a bookmark */
static const char *const chat_flags[] = { "gtk-autojoin", "gtk-persistent" };

static void
copy_contact(PurpleContact *from, PurpleAccount *orig, PurpleAccount *clone,
             guint *buddies)
{
	PurpleBlistNode *n = (PurpleBlistNode *)from;
	PurpleGroup *group = purple_contact_get_group(from);
	PurpleContact *to = NULL;

	for (n = n->child; n != NULL; n = n->next) {
		PurpleBuddy *b = (PurpleBuddy *)n, *nb;

		if (!PURPLE_BLIST_NODE_IS_BUDDY(n) ||
		    purple_buddy_get_account(b) != orig)
			continue;
		if (to == NULL) {
			to = purple_contact_new();
			purple_blist_add_contact(to, group, NULL);
			if (from->alias != NULL)
				purple_blist_alias_contact(to, from->alias);
		}
		nb = purple_buddy_new(clone, purple_buddy_get_name(b),
		                      purple_buddy_get_local_buddy_alias(b));
		purple_blist_add_buddy(nb, to, group, NULL);
		(*buddies)++;
	}
}

static void
copy_chat(PurpleChat *from, PurpleAccount *clone)
{
	GHashTable *comps = g_hash_table_new_full(g_str_hash, g_str_equal,
	                                          g_free, g_free);
	GHashTableIter it;
	gpointer k, v;
	PurpleChat *to;
	guint i;

	g_hash_table_iter_init(&it, purple_chat_get_components(from));
	while (g_hash_table_iter_next(&it, &k, &v))
		g_hash_table_insert(comps, g_strdup(k), g_strdup(v));

	to = purple_chat_new(clone, from->alias, comps);
	purple_blist_add_chat(to, purple_chat_get_group(from), NULL);
	for (i = 0; i < G_N_ELEMENTS(chat_flags); i++)
		if (purple_blist_node_get_bool((PurpleBlistNode *)from,
		                               chat_flags[i]))
			purple_blist_node_set_bool((PurpleBlistNode *)to,
			                           chat_flags[i], TRUE);
}

void
x2_migrate_copy_blist(PurpleAccount *orig, PurpleAccount *clone,
                      guint *buddies, guint *chats)
{
	GPtrArray *todo = g_ptr_array_new();
	PurpleBlistNode *g, *n;
	guint i;

	g_return_if_fail(orig != NULL && clone != NULL);
	g_return_if_fail(buddies != NULL && chats != NULL);
	*buddies = *chats = 0;

	/* collect first: adding nodes while walking the list is not safe */
	for (g = purple_blist_get_root(); g != NULL; g = g->next) {
		if (!PURPLE_BLIST_NODE_IS_GROUP(g))
			continue;
		for (n = g->child; n != NULL; n = n->next)
			if (PURPLE_BLIST_NODE_IS_CONTACT(n) ||
			    (PURPLE_BLIST_NODE_IS_CHAT(n) &&
			     purple_chat_get_account((PurpleChat *)n) == orig))
				g_ptr_array_add(todo, n);
	}

	for (i = 0; i < todo->len; i++) {
		n = g_ptr_array_index(todo, i);
		if (PURPLE_BLIST_NODE_IS_CHAT(n)) {
			copy_chat((PurpleChat *)n, clone);
			(*chats)++;
		} else
			copy_contact((PurpleContact *)n, orig, clone, buddies);
	}
	g_ptr_array_free(todo, TRUE);
	purple_debug_info(DBG, "copied %u buddies and %u room bookmarks\n",
	                  *buddies, *chats);
}
