/**
 * @file ext.c Hook registry and dispatch
 *
 * Adding a feature: write ext/<feature>.c with a `const X2Ext x2_ext_<feature>`,
 * add its source to CMakeLists.txt, and append an extern declaration and one
 * table entry below.  The table order is the order of the inbound chain
 * (ARCHITECTURE 6): carbons, mam, receipts, markers, correction.
 */
#include "xmpp2_internal.h"

#include "debug.h"

#include "ext/ext.h"

/* extern const X2Ext x2_ext_carbons; ... one line per module */

static const X2Ext *const x2_exts[] = {
	/* &x2_ext_carbons, ... */
	NULL
};

#define FOR_EACH_EXT(e) \
	for (const X2Ext *const *x_ = x2_exts; (e = *x_) != NULL; x_++)

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

	js->x2 = g_new0(X2Stream, 1);
	js->x2->dedup = x2_dedup_new(4096);

	FOR_EACH_EXT(e)
		if (e->stream_new)
			e->stream_new(js);
}

void
x2_ext_stream_free(JabberStream *js)
{
	const X2Ext *e;

	if (js->x2 == NULL)
		return;

	FOR_EACH_EXT(e)
		if (e->stream_free)
			e->stream_free(js);

	x2_dedup_free(js->x2->dedup);
	g_free(js->x2);
	js->x2 = NULL;
}

void
x2_ext_account_info(JabberStream *js, xmlnode *query)
{
	const X2Ext *e;

	FOR_EACH_EXT(e)
		if (e->account_info)
			e->account_info(js, query);
}

void
x2_ext_server_info(JabberStream *js, const char *from, xmlnode *query)
{
	const X2Ext *e;

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
	const X2Ext *e;

	FOR_EACH_EXT(e) {
		X2Verdict v;

		if (e->inbound == NULL)
			continue;
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
