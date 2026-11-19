/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/**
 * @file ext.c Hook registry and dispatch
 *
 * Adding a feature: write ext/<feature>.c with a `const X2Ext x2_ext_<feature>`,
 * add its source to CMakeLists.txt, and append an extern declaration and one
 * table entry below.  The table order is the order of the inbound chain
 * (ARCHITECTURE 6): carbons, mam, receipts, markers, correction.  Per-stream
 * module state goes through x2_ext_get_priv(), not through shared headers.
 */
#include "xmpp2_internal.h"

#include "debug.h"

#include "ext/ext.h"
#include "jutil.h"
#include "namespaces.h"

#ifdef X2_EXT_TEST_TABLE
/* Unit tests link this file with their own module table. */
extern const X2Ext *const X2_EXT_TEST_TABLE[];
#define x2_exts X2_EXT_TEST_TABLE
#else
extern const X2Ext x2_ext_carbons;
extern const X2Ext x2_ext_mam;
extern const X2Ext x2_ext_correct;
extern const X2Ext x2_ext_upload;
/* ... one line per module */

static const X2Ext *const x2_exts[] = {
	&x2_ext_carbons,
	&x2_ext_mam,
	&x2_ext_correct,
	&x2_ext_upload,
	/* ... */
	NULL
};

G_STATIC_ASSERT(G_N_ELEMENTS(x2_exts) - 1 <= X2_EXT_MAX);
#endif

#define FOR_EACH_EXT(e) \
	for (const X2Ext *const *x_ = x2_exts; (e = *x_) != NULL; x_++)

static int
ext_index(const X2Ext *ext)
{
	int i;

	for (i = 0; x2_exts[i] != NULL; i++)
		if (x2_exts[i] == ext)
			return i;
	return -1;
}

void
x2_ext_plugin_init(PurplePlugin *plugin)
{
	const X2Ext *e;

	FOR_EACH_EXT(e)
		if (e->plugin_init)
			e->plugin_init(plugin);
}

void
x2_ext_plugin_uninit(PurplePlugin *plugin)
{
	const X2Ext *e;

	FOR_EACH_EXT(e)
		if (e->plugin_uninit)
			e->plugin_uninit(plugin);
}

void
x2_ext_account_options(GList **opts)
{
	const X2Ext *e;

	FOR_EACH_EXT(e)
		if (e->account_options)
			e->account_options(opts);
}

void
x2_ext_stream_new(JabberStream *js)
{
	const X2Ext *e;
	int n;

	g_return_if_fail(js != NULL);
	g_return_if_fail(js->x2 == NULL);

	for (n = 0; x2_exts[n] != NULL; n++)
		;
	if (n > X2_EXT_MAX)
		g_error("xmpp2: %d modules registered, X2_EXT_MAX is %d",
		        n, X2_EXT_MAX);

	js->x2 = g_new0(X2Stream, 1);
	js->x2->dedup = x2_dedup_new(4096);
	js->x2->server_items = g_hash_table_new_full(g_str_hash, g_str_equal,
	                                             g_free, NULL);
	js->x2->trusted_archives = g_hash_table_new_full(g_str_hash,
	                                                 g_str_equal,
	                                                 g_free, NULL);

	FOR_EACH_EXT(e)
		if (e->stream_new)
			e->stream_new(js);
}

void
x2_ext_stream_free(JabberStream *js)
{
	const X2Ext *e;
	int i;

	g_return_if_fail(js != NULL);

	if (js->x2 == NULL)
		return;

	FOR_EACH_EXT(e)
		if (e->stream_free)
			e->stream_free(js);

	/* Only table positions can be set, see x2_ext_set_priv() */
	for (i = 0; x2_exts[i] != NULL; i++)
		if (js->x2->priv[i] != NULL)
			g_critical("xmpp2: module %s leaked its stream state",
			           x2_exts[i]->name);

	x2_dedup_free(js->x2->dedup);
	g_hash_table_destroy(js->x2->server_items);
	g_hash_table_destroy(js->x2->trusted_archives);
	g_free(js->x2);
	js->x2 = NULL;
}

/* Does the disco#info query advertise this feature? */
static gboolean
query_has_feature(xmlnode *query, const char *var)
{
	xmlnode *f;

	for (f = xmlnode_get_child(query, "feature"); f;
	     f = xmlnode_get_next_twin(f))
		if (purple_strequal(xmlnode_get_attrib(f, "var"), var))
			return TRUE;
	return FALSE;
}

void
x2_ext_server_info(JabberStream *js, const char *from, xmlnode *query)
{
	const X2Ext *e;
	char *bare;

	g_return_if_fail(js != NULL && js->x2 != NULL);
	g_return_if_fail(from != NULL);
	g_return_if_fail(query != NULL);

	bare = jabber_id_get_bare_jid(js->user);
	if (!purple_strequal(from, bare) && !x2_ext_is_server_item(js, from)) {
		/* disco.c filters; anything else is a caller bug (ext.h) */
		g_critical("x2_ext_server_info: untrusted source %s", from);
		g_free(bare);
		return;
	}
	if (purple_strequal(from, bare) &&
	    (query_has_feature(query, NS_SID) || query_has_feature(query, NS_MAM)))
		x2_ext_archive_trust(js, bare);
	g_free(bare);

	FOR_EACH_EXT(e)
		if (e->server_info)
			e->server_info(js, from, query);
}

void
x2_ext_pre_presence(JabberStream *js)
{
	const X2Ext *e;

	purple_debug_info("xmpp2", "hook pre_presence\n");
	FOR_EACH_EXT(e)
		if (e->pre_presence)
			e->pre_presence(js);
}

void
x2_ext_session_ready(JabberStream *js)
{
	const X2Ext *e;

	purple_debug_info("xmpp2", "hook session_ready\n");
	FOR_EACH_EXT(e)
		if (e->session_ready)
			e->session_ready(js);
}

X2Verdict
x2_ext_inbound(JabberStream *js, X2Msg *m)
{
	int i;

	g_return_val_if_fail(m != NULL, X2_CONSUMED);
	g_return_val_if_fail(m->stage >= -1, X2_CONSUMED);

	for (i = m->stage + 1; x2_exts[i] != NULL; i++) {
		const X2Ext *e = x2_exts[i];
		X2Verdict v;

		if (e->inbound == NULL)
			continue;
		m->stage = i;
		v = e->inbound(js, m);
		if (v != X2_PASS) {
			purple_debug_info("xmpp2", "inbound message %s by %s\n",
			                  v == X2_CONSUMED ? "consumed" : "deferred",
			                  e->name);
			return v;
		}
	}
	return X2_PASS;
}

void
x2_ext_outbound(JabberStream *js, xmlnode *message, X2Out *o)
{
	const X2Ext *e;

	FOR_EACH_EXT(e)
		if (e->outbound)
			e->outbound(js, message, o);
}

void
x2_ext_chat_joined(JabberStream *js, JabberChat *chat)
{
	const X2Ext *e;

	FOR_EACH_EXT(e)
		if (e->chat_joined)
			e->chat_joined(js, chat);
}

void
x2_ext_stream_closing(JabberStream *js)
{
	const X2Ext *e;

	g_return_if_fail(js != NULL);
	if (js->x2 == NULL)
		return;

	FOR_EACH_EXT(e)
		if (e->stream_closing)
			e->stream_closing(js);
}

void
x2_ext_carbons_settled(JabberStream *js)
{
	const X2Ext *e;

	g_return_if_fail(js != NULL && js->x2 != NULL);

	purple_debug_info("xmpp2", "hook carbons_settled\n");
	FOR_EACH_EXT(e)
		if (e->carbons_settled)
			e->carbons_settled(js);
}

gboolean
x2_ext_registered(const char *name)
{
	const X2Ext *e;

	FOR_EACH_EXT(e)
		if (purple_strequal(e->name, name))
			return TRUE;
	return FALSE;
}

void
x2_ext_server_item_add(JabberStream *js, const char *jid)
{
	g_return_if_fail(js != NULL && js->x2 != NULL);
	g_return_if_fail(jid != NULL && *jid != '\0');

	g_hash_table_add(js->x2->server_items, g_strdup(jid));
}

gboolean
x2_ext_is_server_item(JabberStream *js, const char *jid)
{
	g_return_val_if_fail(js != NULL && js->x2 != NULL, FALSE);

	if (jid == NULL)
		return FALSE;
	return purple_strequal(jid, js->user->domain) ||
	       g_hash_table_contains(js->x2->server_items, jid);
}

void
x2_ext_archive_trust(JabberStream *js, const char *archive)
{
	g_return_if_fail(js != NULL && js->x2 != NULL);
	g_return_if_fail(archive != NULL && *archive != '\0');

	g_hash_table_add(js->x2->trusted_archives, g_strdup(archive));
}

gboolean
x2_ext_archive_is_trusted(JabberStream *js, const char *archive)
{
	g_return_val_if_fail(js != NULL && js->x2 != NULL, FALSE);

	return archive != NULL &&
	       g_hash_table_contains(js->x2->trusted_archives, archive);
}

gpointer
x2_ext_get_priv(JabberStream *js, const X2Ext *ext)
{
	int i;

	g_return_val_if_fail(js != NULL && js->x2 != NULL, NULL);
	i = ext_index(ext);
	g_return_val_if_fail(i >= 0, NULL);

	return js->x2->priv[i];
}

void
x2_ext_set_priv(JabberStream *js, const X2Ext *ext, gpointer priv)
{
	int i;

	g_return_if_fail(js != NULL && js->x2 != NULL);
	i = ext_index(ext);
	g_return_if_fail(i >= 0);

	js->x2->priv[i] = priv;
}
