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
