/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file forward.c XEP-0297 stanza forwarding, XEP-0203 delayed delivery
 */
#include "forward.h"

#include "util.h"

#define X2_NS_DELAY "urn:xmpp:delay"
#define X2_NS_DELAY_LEGACY "jabber:x:delay"

xmlnode *
x2_forward_get_forwarded(const xmlnode *container)
{
	g_return_val_if_fail(container != NULL, NULL);

	return xmlnode_get_child_with_namespace(container, "forwarded",
	                                        X2_NS_FORWARD);
}

xmlnode *
x2_forward_get_message(const xmlnode *container)
{
	xmlnode *fwd = x2_forward_get_forwarded(container);

	if (fwd == NULL)
		return NULL;
	/* The wrapped stanza is a plain <message/> in the client namespace; do
	 * not insist on the namespace, some servers leave it implicit. */
	return xmlnode_get_child(fwd, "message");
}

gboolean
x2_delay_parse(const xmlnode *stanza, time_t *ts, char **from)
{
	xmlnode *delay;
	const char *stamp;
	time_t t;

	g_return_val_if_fail(stanza != NULL, FALSE);
	g_return_val_if_fail(ts != NULL, FALSE);

	if (from != NULL)
		*from = NULL;

	delay = xmlnode_get_child_with_namespace(stanza, "delay", X2_NS_DELAY);
	if (delay == NULL)
		delay = xmlnode_get_child_with_namespace(stanza, "x",
		                                         X2_NS_DELAY_LEGACY);
	if (delay == NULL)
		return FALSE;

	stamp = xmlnode_get_attrib(delay, "stamp");
	if (stamp == NULL || *stamp == '\0')
		return FALSE;

	/* purple_str_to_time() returns 0 on parse errors */
	t = purple_str_to_time(stamp, TRUE, NULL, NULL, NULL);
	if (t == 0)
		return FALSE;

	*ts = t;
	if (from != NULL)
		*from = g_strdup(xmlnode_get_attrib(delay, "from"));
	return TRUE;
}

gboolean
x2_forward_delay_parse(const xmlnode *message, time_t *ts)
{
	const xmlnode *parent;

	g_return_val_if_fail(message != NULL, FALSE);
	g_return_val_if_fail(ts != NULL, FALSE);

	parent = message->parent;
	if (parent != NULL && g_strcmp0(parent->name, "forwarded") == 0 &&
	    g_strcmp0(xmlnode_get_namespace((xmlnode *) parent),
	              X2_NS_FORWARD) == 0 &&
	    x2_delay_parse(parent, ts, NULL))
		return TRUE;

	return x2_delay_parse(message, ts, NULL);
}

xmlnode *
x2_forward_copy(const xmlnode *outer, const xmlnode *node,
                xmlnode **node_copy)
{
	GArray *path;
	const xmlnode *n;
	xmlnode *copy, *cur;
	guint i;

	g_return_val_if_fail(outer != NULL, NULL);
	g_return_val_if_fail(node != NULL, NULL);
	g_return_val_if_fail(node_copy != NULL, NULL);

	*node_copy = NULL;

	/* Child positions from node up to outer.  xmlnode_copy() keeps every
	 * child (tags, data, attributes) in order, so they hold in the copy. */
	path = g_array_new(FALSE, FALSE, sizeof(guint));
	for (n = node; n != outer; n = n->parent) {
		const xmlnode *sib;
		guint pos = 0;

		if (n->parent == NULL) {
			g_array_free(path, TRUE);
			g_critical("x2_forward_copy: node is not inside outer");
			return NULL;
		}
		for (sib = n->parent->child; sib != n; sib = sib->next)
			pos++;
		g_array_append_val(path, pos);
	}

	copy = xmlnode_copy(outer);
	cur = copy;
	for (i = path->len; i > 0; i--) {
		guint pos = g_array_index(path, guint, i - 1);

		for (cur = cur->child; pos > 0; pos--)
			cur = cur->next;
	}
	g_array_free(path, TRUE);

	*node_copy = cur;
	return copy;
}
