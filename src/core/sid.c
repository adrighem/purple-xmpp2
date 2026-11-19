/**
 * @file sid.c XEP-0359 unique and stable stanza ids (pure logic)
 */
#include "sid.h"

char *
x2_sid_new_id(void)
{
	return g_uuid_string_random();
}

static xmlnode *
find_child(const xmlnode *message, const char *name, const char *by)
{
	xmlnode *child;

	for (child = xmlnode_get_child_with_namespace(message, name, X2_NS_SID);
	     child;
	     child = xmlnode_get_next_twin(child)) {
		const char *id = xmlnode_get_attrib(child, "id");

		if (id == NULL || *id == '\0')
			continue;
		if (by != NULL && g_strcmp0(by, xmlnode_get_attrib(child, "by")) != 0)
			continue;
		return child;
	}
	return NULL;
}

char *
x2_sid_origin_id(const xmlnode *message)
{
	xmlnode *child;

	g_return_val_if_fail(message != NULL, NULL);

	child = find_child(message, "origin-id", NULL);
	return child ? g_strdup(xmlnode_get_attrib(child, "id")) : NULL;
}

char *
x2_sid_stanza_id(const xmlnode *message, const char *archive)
{
	xmlnode *child;

	g_return_val_if_fail(message != NULL, NULL);
	g_return_val_if_fail(archive != NULL && *archive != '\0', NULL);

	child = find_child(message, "stanza-id", archive);
	return child ? g_strdup(xmlnode_get_attrib(child, "id")) : NULL;
}

void
x2_sid_add_origin_id(xmlnode *message, const char *id)
{
	xmlnode *child;

	g_return_if_fail(message != NULL);
	g_return_if_fail(id != NULL && *id != '\0');

	child = xmlnode_new_child(message, "origin-id");
	xmlnode_set_namespace(child, X2_NS_SID);
	xmlnode_set_attrib(child, "id", id);
}
