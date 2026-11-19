/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file receipt_parse.c XEP-0184 receipts and XEP-0333 chat markers (pure logic)
 */
#include <string.h>

#include "receipt_parse.h"
#include "jid.h"

static const struct {
	X2MarkerType type;
	const char *name;
} marker_names[] = {
	{ X2_MARKER_RECEIVED, "received" },
	{ X2_MARKER_DISPLAYED, "displayed" },
	{ X2_MARKER_ACKNOWLEDGED, "acknowledged" },
};

/* Only chat-like messages with an id can be acknowledged. */
static const char *
answerable_id(const xmlnode *message)
{
	const char *type = xmlnode_get_attrib(message, "type");
	const char *id = xmlnode_get_attrib(message, "id");

	if (id == NULL || *id == '\0')
		return NULL;
	if (type != NULL && g_strcmp0(type, "chat") != 0 &&
	    g_strcmp0(type, "normal") != 0)
		return NULL;
	return id;
}

static const char *
child_id(const xmlnode *message, const char *name, const char *ns)
{
	xmlnode *child = xmlnode_get_child_with_namespace(message, name, ns);
	const char *id = child ? xmlnode_get_attrib(child, "id") : NULL;

	return (id != NULL && *id != '\0') ? id : NULL;
}

char *
x2_receipt_request_id(const xmlnode *message)
{
	const char *id;

	g_return_val_if_fail(message != NULL, NULL);

	id = answerable_id(message);
	if (id == NULL ||
	    xmlnode_get_child_with_namespace(message, "request",
	                                     X2_NS_RECEIPTS) == NULL ||
	    xmlnode_get_child_with_namespace(message, "received",
	                                     X2_NS_RECEIPTS) != NULL)
		return NULL;
	return g_strdup(id);
}

char *
x2_receipt_received_id(const xmlnode *message)
{
	g_return_val_if_fail(message != NULL, NULL);

	return g_strdup(child_id(message, "received", X2_NS_RECEIPTS));
}

void
x2_receipt_add_request(xmlnode *message)
{
	g_return_if_fail(message != NULL);

	xmlnode_set_namespace(xmlnode_new_child(message, "request"),
	                      X2_NS_RECEIPTS);
}

xmlnode *
x2_receipt_new_received(const char *to, const char *id)
{
	xmlnode *message, *received;

	g_return_val_if_fail(to != NULL && id != NULL, NULL);

	message = xmlnode_new("message");
	xmlnode_set_attrib(message, "to", to);
	received = xmlnode_new_child(message, "received");
	xmlnode_set_namespace(received, X2_NS_RECEIPTS);
	xmlnode_set_attrib(received, "id", id);
	return message;
}

char *
x2_marker_markable_id(const xmlnode *message)
{
	const char *id;

	g_return_val_if_fail(message != NULL, NULL);

	id = answerable_id(message);
	if (id == NULL ||
	    xmlnode_get_child_with_namespace(message, "markable",
	                                     X2_NS_CHAT_MARKERS) == NULL)
		return NULL;
	return g_strdup(id);
}

const char *
x2_marker_name(X2MarkerType type)
{
	guint i;

	for (i = 0; i < G_N_ELEMENTS(marker_names); i++)
		if (marker_names[i].type == type)
			return marker_names[i].name;
	return NULL;
}

X2MarkerType
x2_marker_parse(const xmlnode *message, char **id)
{
	guint i;

	g_return_val_if_fail(message != NULL && id != NULL, X2_MARKER_NONE);

	for (i = 0; i < G_N_ELEMENTS(marker_names); i++) {
		const char *found = child_id(message, marker_names[i].name,
		                             X2_NS_CHAT_MARKERS);

		if (found != NULL) {
			*id = g_strdup(found);
			return marker_names[i].type;
		}
	}
	return X2_MARKER_NONE;
}

void
x2_marker_add_markable(xmlnode *message)
{
	g_return_if_fail(message != NULL);

	xmlnode_set_namespace(xmlnode_new_child(message, "markable"),
	                      X2_NS_CHAT_MARKERS);
}

xmlnode *
x2_marker_new(const char *to, X2MarkerType type, const char *id)
{
	xmlnode *message, *marker;

	g_return_val_if_fail(to != NULL && id != NULL, NULL);
	g_return_val_if_fail(x2_marker_name(type) != NULL, NULL);

	message = xmlnode_new("message");
	xmlnode_set_attrib(message, "to", to);
	xmlnode_set_attrib(message, "type", "chat");
	marker = xmlnode_new_child(message, x2_marker_name(type));
	xmlnode_set_namespace(marker, X2_NS_CHAT_MARKERS);
	xmlnode_set_attrib(marker, "id", id);
	return message;
}

char *
x2_ack_key(const char *jid, const char *id)
{
	char *bare, *lower, *key;

	g_return_val_if_fail(jid != NULL && id != NULL, NULL);

	bare = x2_jid_bare(jid);
	lower = g_ascii_strdown(bare != NULL ? bare : "", -1);
	key = g_strdup_printf("%s\x1f%s", lower, id);
	g_free(lower);
	g_free(bare);
	return key;
}
