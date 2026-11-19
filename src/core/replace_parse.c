/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file replace_parse.c XEP-0308 correction element: parse, build, mark
 */
#include <string.h>

#include "replace_parse.h"

#define X2_NS_XHTML_IM "http://jabber.org/protocol/xhtml-im"

char *
x2_replace_id(const xmlnode *message)
{
	xmlnode *r;
	const char *id;

	g_return_val_if_fail(message != NULL, NULL);

	r = xmlnode_get_child_with_namespace(message, "replace", X2_NS_REPLACE);
	id = r ? xmlnode_get_attrib(r, "id") : NULL;
	return (id != NULL && *id != '\0') ? g_strdup(id) : NULL;
}

void
x2_replace_add(xmlnode *message, const char *id)
{
	xmlnode *r;

	g_return_if_fail(message != NULL);
	g_return_if_fail(id != NULL && *id != '\0');

	r = xmlnode_new_child(message, "replace");
	xmlnode_set_namespace(r, X2_NS_REPLACE);
	xmlnode_set_attrib(r, "id", id);
}

char *
x2_replace_bare(const char *jid)
{
	const char *slash;

	if (jid == NULL || *jid == '\0')
		return NULL;
	slash = strchr(jid, '/');
	return slash ? g_strndup(jid, slash - jid) : g_strdup(jid);
}

char *
x2_replace_sender(const xmlnode *message, const char *from,
                  gboolean groupchat, gboolean trust_occupant_id)
{
	xmlnode *occ;
	const char *id;

	g_return_val_if_fail(message != NULL, NULL);

	if (from == NULL || *from == '\0')
		return NULL;
	if (!groupchat)
		return x2_replace_bare(from);
	if (!trust_occupant_id)
		return g_strdup(from);

	occ = xmlnode_get_child_with_namespace(message, "occupant-id",
	                                       X2_NS_OCCUPANT_ID);
	id = occ ? xmlnode_get_attrib(occ, "id") : NULL;
	if (id != NULL && *id != '\0')
		return g_strconcat("occ:", id, NULL);
	return g_strdup(from);
}

gboolean
x2_replace_mark_body(xmlnode *message, const char *marker)
{
	xmlnode *body, *html;
	char *text, *ns, *marked;

	g_return_val_if_fail(message != NULL, FALSE);
	g_return_val_if_fail(marker != NULL, FALSE);

	body = xmlnode_get_child(message, "body");
	text = body ? xmlnode_get_data(body) : NULL;
	if (text == NULL || *text == '\0') {
		g_free(text);
		return FALSE;
	}

	/* the replacement must carry the namespace the delivery code checks */
	ns = g_strdup(xmlnode_get_namespace(body));
	xmlnode_free(body);
	body = xmlnode_new_child(message, "body");
	if (ns != NULL)
		xmlnode_set_namespace(body, ns);
	marked = g_strconcat(marker, text, NULL);
	xmlnode_insert_data(body, marked, -1);

	while ((html = xmlnode_get_child_with_namespace(message, "html",
	                                                X2_NS_XHTML_IM)) != NULL)
		xmlnode_free(html);

	g_free(marked);
	g_free(ns);
	g_free(text);
	return TRUE;
}
