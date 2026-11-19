/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file carbon_parse.c XEP-0280 message carbons: classify and validate
 */
#include <string.h>

#include "carbon_parse.h"
#include "forward.h"

/* TRUE if jid is own_bare, optionally followed by a /resource. */
static gboolean
jid_is(const char *jid, const char *own_bare, gboolean allow_resource)
{
	size_t len;

	if (jid == NULL || own_bare == NULL)
		return FALSE;

	len = strlen(own_bare);
	if (len == 0 || g_ascii_strncasecmp(jid, own_bare, len) != 0)
		return FALSE;
	return jid[len] == '\0' || (allow_resource && jid[len] == '/');
}

static X2CarbonKind
reject(const char **reason, const char *why)
{
	if (reason != NULL)
		*reason = why;
	return X2_CARBON_REJECTED;
}

X2CarbonKind
x2_carbon_classify(const xmlnode *outer, const char *own_bare,
                   xmlnode **inner, const char **reason)
{
	xmlnode *sent, *received, *wrapper, *msg;
	const char *from, *type, *peer_jid, *own_jid;
	X2CarbonKind kind;

	g_return_val_if_fail(outer != NULL, X2_CARBON_NONE);
	g_return_val_if_fail(own_bare != NULL && *own_bare != '\0',
	                     X2_CARBON_NONE);

	if (inner != NULL)
		*inner = NULL;
	if (reason != NULL)
		*reason = NULL;

	sent = xmlnode_get_child_with_namespace(outer, "sent", X2_NS_CARBONS);
	received = xmlnode_get_child_with_namespace(outer, "received",
	                                            X2_NS_CARBONS);
	if (sent == NULL && received == NULL)
		return X2_CARBON_NONE;

	/* From here on it claims to be a carbon: anything off is forged. */
	from = xmlnode_get_attrib(outer, "from");
	if (from != NULL && !jid_is(from, own_bare, FALSE))
		return reject(reason, "outer from is not our bare JID");

	if ((sent != NULL && received != NULL) ||
	    (sent != NULL && xmlnode_get_next_twin(sent) != NULL) ||
	    (received != NULL && xmlnode_get_next_twin(received) != NULL))
		return reject(reason, "ambiguous carbon wrapper");

	kind = sent != NULL ? X2_CARBON_SENT : X2_CARBON_RECEIVED;
	wrapper = sent != NULL ? sent : received;

	msg = x2_forward_get_message(wrapper);
	if (msg == NULL)
		return reject(reason, "no forwarded message");

	type = xmlnode_get_attrib(msg, "type");
	if (type != NULL && strcmp(type, "chat") != 0 &&
	    strcmp(type, "normal") != 0)
		return reject(reason, "inner message type not chat or normal");

	if (kind == X2_CARBON_SENT) {
		own_jid = xmlnode_get_attrib(msg, "from");
		peer_jid = xmlnode_get_attrib(msg, "to");
	} else {
		own_jid = xmlnode_get_attrib(msg, "to");
		peer_jid = xmlnode_get_attrib(msg, "from");
	}
	if (!jid_is(own_jid, own_bare, TRUE))
		return reject(reason, kind == X2_CARBON_SENT
		              ? "sent carbon inner from is not our JID"
		              : "received carbon inner to is not our JID");
	if (peer_jid == NULL || *peer_jid == '\0')
		return reject(reason, "inner message has no peer JID");

	if (inner != NULL)
		*inner = msg;
	return kind;
}
