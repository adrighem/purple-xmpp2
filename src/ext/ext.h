/**
 * @file ext.h Hook registry of the xmpp2 feature modules
 *
 * The only coupling between the inherited jabber code and the new features.
 * Upstream files include this header and call the x2_ext_*() dispatchers at
 * a handful of fixed places; each feature (XEP) is one module implementing
 * the hooks it needs and listed once in the table in ext.c.
 */
#ifndef XMPP2_EXT_EXT_H_
#define XMPP2_EXT_EXT_H_

#include <glib.h>
#include "plugin.h"
#include "xmlnode.h"

#include "jabber.h"
#include "chat.h"
#include "ext/x2types.h"
#include "ext/x2stream.h"
#include "ext/msgctx.h"

typedef enum {
	X2_PASS,       /* not handled, continue with the next module */
	X2_CONSUMED,   /* handled, stop, do not deliver */
	X2_DEFERRED    /* queued by the module, which will redeliver later */
} X2Verdict;

/** What the outbound hooks may need to know about a message being sent. */
struct _X2Out {
	gboolean groupchat;   /* type='groupchat' */
	gboolean has_body;
	const char *id;       /* stanza id, equal to the origin-id */
};

typedef struct {
	const char *name;
	void (*plugin_init)(PurplePlugin *p);      /* features, cmds, signals */
	void (*plugin_uninit)(PurplePlugin *p);
	void (*account_options)(GList **opts);     /* append PurpleAccountOption */
	void (*stream_new)(JabberStream *js);
	void (*stream_free)(JabberStream *js);
	/* own bare JID disco#info result */
	void (*account_info)(JabberStream *js, xmlnode *query);
	/* disco#info of the server domain and of its components */
	void (*server_info)(JabberStream *js, const char *from, xmlnode *query);
	void (*pre_presence)(JabberStream *js);    /* roster done, before initial presence */
	void (*session_ready)(JabberStream *js);   /* PURPLE_CONNECTED */
	X2Verdict (*inbound)(JabberStream *js, X2Msg *m);
	void (*outbound)(JabberStream *js, xmlnode *message, X2Out *o);
	void (*chat_joined)(JabberStream *js, JabberChat *chat);
} X2Ext;

/* Dispatchers, called from the inherited code. Safe with no modules. */
void x2_ext_plugin_init(PurplePlugin *plugin);
void x2_ext_plugin_uninit(PurplePlugin *plugin);
void x2_ext_account_options(GList **opts);
void x2_ext_stream_new(JabberStream *js);
void x2_ext_stream_free(JabberStream *js);
void x2_ext_account_info(JabberStream *js, xmlnode *query);
void x2_ext_server_info(JabberStream *js, const char *from, xmlnode *query);
void x2_ext_pre_presence(JabberStream *js);
void x2_ext_session_ready(JabberStream *js);
X2Verdict x2_ext_inbound(JabberStream *js, X2Msg *m);
void x2_ext_outbound(JabberStream *js, xmlnode *message, X2Out *o);
void x2_ext_chat_joined(JabberStream *js, JabberChat *chat);

#endif /* XMPP2_EXT_EXT_H_ */
