/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
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
	/* not handled, continue with the next module */
	X2_PASS,
	/* handled, stop, do not deliver */
	X2_CONSUMED,
	/* the module kept an x2_msg_dup() of the message and will hand it back
	 * with x2_msg_redeliver(); the original is freed by the pipeline */
	X2_DEFERRED
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
	/*
	 * disco#info result of a trusted source: our server domain, a JID
	 * listed in our server's disco#items (upload, MUC, proxy components),
	 * or our own bare JID (account features: MAM and carbons on Prosody).
	 * Compare @a from with js->user to tell them apart.  Results of any
	 * other entity (contacts, foreign services) never reach this hook.  May
	 * run more than once for the same JID: keep it idempotent.
	 */
	void (*server_info)(JabberStream *js, const char *from, xmlnode *query);
	void (*pre_presence)(JabberStream *js);    /* roster done, before initial presence */
	void (*session_ready)(JabberStream *js);   /* PURPLE_CONNECTED */
	X2Verdict (*inbound)(JabberStream *js, X2Msg *m);
	void (*outbound)(JabberStream *js, xmlnode *message, X2Out *o);
	/*
	 * A room is about to be joined (jabber_join_chat()).  Return TRUE to
	 * hold the join presence: the module then calls jabber_chat_send_join()
	 * itself later (e.g. after the room's disco#info).  The first module
	 * returning TRUE wins; the rest are not asked.
	 */
	gboolean (*chat_joining)(JabberStream *js, JabberChat *chat);
	/* The <x xmlns='http://jabber.org/protocol/muc'/> of a join presence,
	 * after the user's history settings: adjust it (e.g. <history/>). */
	void (*chat_join_presence)(JabberStream *js, JabberChat *chat,
	                           xmlnode *muc_x);
	/* Once per join: our first self-presence of the room (status 110),
	 * never on a nick change or a later presence update. */
	void (*chat_joined)(JabberStream *js, JabberChat *chat);
	/*
	 * Start of jabber_close(), while the connection, buddies and chats
	 * still exist: last chance to deliver queued messages and save state.
	 * stream_free runs later, after most of JabberStream is gone.
	 */
	void (*stream_closing)(JabberStream *js);
	/*
	 * The carbons module got the result (or error) of its <enable/> IQ.
	 * The account MAM catch-up waits for it (ARCHITECTURE 5, 7.1).
	 */
	void (*carbons_settled)(JabberStream *js);
} X2Ext;

/* Dispatchers, called from the inherited code. Safe with no modules. */
void x2_ext_plugin_init(PurplePlugin *plugin);
void x2_ext_plugin_uninit(PurplePlugin *plugin);
void x2_ext_account_options(GList **opts);
void x2_ext_stream_new(JabberStream *js);
void x2_ext_stream_free(JabberStream *js);
/* from must be a trusted source (see server_info above), else critical */
void x2_ext_server_info(JabberStream *js, const char *from, xmlnode *query);
void x2_ext_pre_presence(JabberStream *js);
void x2_ext_session_ready(JabberStream *js);
void x2_ext_outbound(JabberStream *js, xmlnode *message, X2Out *o);
/* TRUE if a module holds the join presence (see chat_joining above) */
gboolean x2_ext_chat_joining(JabberStream *js, JabberChat *chat);
void x2_ext_chat_join_presence(JabberStream *js, JabberChat *chat,
                               xmlnode *muc_x);
void x2_ext_chat_joined(JabberStream *js, JabberChat *chat);
void x2_ext_stream_closing(JabberStream *js);

/**
 * Called by the carbons module once per stream after its <enable/> IQ was
 * answered (result or error), or at once if the server has no carbons.
 * Runs the carbons_settled hooks; the account MAM query is sent only
 * afterwards, so no message falls between catch-up and carbons (ARCHITECTURE
 * 5, 7.1).  Never calling it is covered by the MAM watchdog.
 */
void x2_ext_carbons_settled(JabberStream *js);

/** TRUE if a module with this name is in the registry. */
gboolean x2_ext_registered(const char *name);

/**
 * Runs the inbound hooks of the modules after m->stage (all of them for a
 * new context) and records in m->stage the module that is running, so a
 * module can x2_msg_dup() the context and later resume after itself.
 */
X2Verdict x2_ext_inbound(JabberStream *js, X2Msg *m);

/** Remembers a JID from our own server's disco#items. */
void x2_ext_server_item_add(JabberStream *js, const char *jid);

/** TRUE for our server domain and the JIDs in its disco#items. */
gboolean x2_ext_is_server_item(JabberStream *js, const char *jid);

/**
 * Marks an archive (a room JID after its disco#info) as stamping reliable
 * XEP-0359 stanza-ids.  The own bare JID is marked by x2_ext_server_info()
 * when the account advertises urn:xmpp:sid:0 or urn:xmpp:mam:2.
 */
void x2_ext_archive_trust(JabberStream *js, const char *archive);

/** TRUE if stanza-ids with by=@a archive may be trusted (ARCHITECTURE 7.3). */
gboolean x2_ext_archive_is_trusted(JabberStream *js, const char *archive);

/**
 * Per-stream private data of a module: the slot in JabberStream::x2->priv
 * that belongs to @a ext (by its position in the registry).
 */
gpointer x2_ext_get_priv(JabberStream *js, const X2Ext *ext);
void x2_ext_set_priv(JabberStream *js, const X2Ext *ext, gpointer priv);

#endif /* XMPP2_EXT_EXT_H_ */
