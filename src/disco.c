/*
 * purple - Jabber Service Discovery
 *
 * Purple is the legal property of its developers, whose names are too numerous
 * to list here.  Please refer to the COPYRIGHT file distributed with this
 * source distribution.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA
 *
 */

#include "xmpp2_internal.h"
#include "network.h"
#include "prefs.h"
#include "debug.h"
#include "request.h"

#include "adhoccommands.h"
#include "buddy.h"
#include "disco.h"
#include "iq.h"
#include "jabber.h"
#include "pep.h"
#include "presence.h"
#include "roster.h"
#include "useravatar.h"
#include "ext/ext.h"

struct _jabber_disco_info_cb_data {
	gpointer data;
	JabberDiscoInfoCallback *callback;
};

struct _jabber_disco_items_cb_data {
	gpointer data;
	JabberDiscoItemsCallback *callback;
};

#define SUPPORT_FEATURE(x) { \
	feature = xmlnode_new_child(query, "feature"); \
	xmlnode_set_attrib(feature, "var", x); \
}

static void
jabber_disco_bytestream_server_cb(JabberStream *js, const char *from,
                                  JabberIqType type, const char *id,
                                  xmlnode *packet, gpointer data)
{
	JabberBytestreamsStreamhost *sh = data;
	xmlnode *query = xmlnode_get_child_with_namespace(packet, "query",
		NS_BYTESTREAMS);

	if (from && purple_strequal(from, sh->jid) && query != NULL) {
		xmlnode *sh_node = xmlnode_get_child(query, "streamhost");
		if (sh_node) {
			const char *jid = xmlnode_get_attrib(sh_node, "jid");
			const char *port = xmlnode_get_attrib(sh_node, "port");


			if (jid == NULL || !purple_strequal(jid, from))
				purple_debug_error("xmpp2", "Invalid jid(%s) for bytestream.\n",
						   jid ? jid : "(null)");

			sh->host = g_strdup(xmlnode_get_attrib(sh_node, "host"));
			sh->zeroconf = g_strdup(xmlnode_get_attrib(sh_node, "zeroconf"));
			if (port != NULL)
				sh->port = atoi(port);
		}
	}

	purple_debug_info("xmpp2", "Discovered bytestream proxy server: "
			  "jid='%s' host='%s' port='%d' zeroconf='%s'\n",
			   from ? from : "", sh->host ? sh->host : "",
			   sh->port, sh->zeroconf ? sh->zeroconf : "");

	/* TODO: When we support zeroconf proxies, fix this to handle them */
	if (!(sh->jid && sh->host && sh->port > 0)) {
		js->bs_proxies = g_list_remove(js->bs_proxies, sh);
		g_free(sh->jid);
		g_free(sh->host);
		g_free(sh->zeroconf);
		g_free(sh);
	}
}


void jabber_disco_info_parse(JabberStream *js, const char *from,
                             JabberIqType type, const char *id,
                             xmlnode *in_query)
{
	if(type == JABBER_IQ_GET) {
		xmlnode *query, *identity, *feature;
		JabberIq *iq;
		const char *node = xmlnode_get_attrib(in_query, "node");
		char *node_uri = NULL;

		/* create custom caps node URI */
		node_uri = g_strconcat(CAPS0115_NODE, "#", jabber_caps_get_own_hash(js), NULL);

		iq = jabber_iq_new_query(js, JABBER_IQ_RESULT, NS_DISCO_INFO);

		jabber_iq_set_id(iq, id);

		if (from)
			xmlnode_set_attrib(iq->node, "to", from);
		query = xmlnode_get_child(iq->node, "query");

		if(node)
			xmlnode_set_attrib(query, "node", node);

		if(!node || purple_strequal(node, node_uri)) {
			GList *features, *identities;
			for(identities = jabber_identities; identities; identities = identities->next) {
				JabberIdentity *ident = (JabberIdentity*)identities->data;
				identity = xmlnode_new_child(query, "identity");
				xmlnode_set_attrib(identity, "category", ident->category);
				xmlnode_set_attrib(identity, "type", ident->type);
				if (ident->lang)
					xmlnode_set_attrib(identity, "xml:lang", ident->lang);
				if (ident->name)
					xmlnode_set_attrib(identity, "name", ident->name);
			}
			for(features = jabber_features; features; features = features->next) {
				JabberFeature *feat = (JabberFeature*)features->data;
				if (!feat->is_enabled || feat->is_enabled(js, feat->namespace)) {
					feature = xmlnode_new_child(query, "feature");
					xmlnode_set_attrib(feature, "var", feat->namespace);
				}
			}
		} else {
			xmlnode *error, *inf;

			/* XXX: gross hack, implement jabber_iq_set_type or something */
			xmlnode_set_attrib(iq->node, "type", "error");
			iq->type = JABBER_IQ_ERROR;

			error = xmlnode_new_child(query, "error");
			xmlnode_set_attrib(error, "code", "404");
			xmlnode_set_attrib(error, "type", "cancel");
			inf = xmlnode_new_child(error, "item-not-found");
			xmlnode_set_namespace(inf, NS_XMPP_STANZAS);
		}
		g_free(node_uri);
		jabber_iq_send(iq);
	} else if (type == JABBER_IQ_SET) {
		/* wtf? seriously. wtf‽ */
		JabberIq *iq = jabber_iq_new(js, JABBER_IQ_ERROR);
		xmlnode *error, *bad_request;

		/* Free the <query/> */
		xmlnode_free(xmlnode_get_child(iq->node, "query"));
		/* Add an error */
		error = xmlnode_new_child(iq->node, "error");
		xmlnode_set_attrib(error, "type", "modify");
		bad_request = xmlnode_new_child(error, "bad-request");
		xmlnode_set_namespace(bad_request, NS_XMPP_STANZAS);

		jabber_iq_set_id(iq, id);
		if (from)
			xmlnode_set_attrib(iq->node, "to", from);

		jabber_iq_send(iq);
	}
}

static void jabber_disco_info_cb(JabberStream *js, const char *from,
                                 JabberIqType type, const char *id,
                                 xmlnode *packet, gpointer data)
{
	struct _jabber_disco_info_cb_data *jdicd = data;
	xmlnode *query;

	query = xmlnode_get_child_with_namespace(packet, "query", NS_DISCO_INFO);

	if (type == JABBER_IQ_RESULT && query) {
		xmlnode *child;
		JabberID *jid;
		JabberBuddy *jb;
		JabberBuddyResource *jbr = NULL;
		JabberCapabilities capabilities = JABBER_CAP_NONE;

		if((jid = jabber_id_new(from))) {
			if(jid->resource && (jb = jabber_buddy_find(js, from, TRUE)))
				jbr = jabber_buddy_find_resource(jb, jid->resource);
			jabber_id_free(jid);
		}

		if(jbr)
			capabilities = jbr->capabilities;

		for(child = query->child; child; child = child->next) {
			if(child->type != XMLNODE_TYPE_TAG)
				continue;

			if(purple_strequal(child->name, "identity")) {
				const char *category = xmlnode_get_attrib(child, "category");
				const char *type = xmlnode_get_attrib(child, "type");
				if(!category || !type)
					continue;

				if(purple_strequal(category, "conference") && purple_strequal(type, "text")) {
					/* we found a groupchat or MUC server, add it to the list */
					/* XXX: actually check for protocol/muc or gc-1.0 support */
					js->chat_servers = g_list_prepend(js->chat_servers, g_strdup(from));
				} else if(purple_strequal(category, "directory") && purple_strequal(type, "user")) {
					/* we found a JUD */
					js->user_directories = g_list_prepend(js->user_directories, g_strdup(from));
				} else if(purple_strequal(category, "proxy") && purple_strequal(type, "bytestreams")) {
					/* This is a bytestream proxy */
					JabberIq *iq;
					JabberBytestreamsStreamhost *sh;

					purple_debug_info("xmpp2", "Found bytestream proxy server: %s\n", from);

					sh = g_new0(JabberBytestreamsStreamhost, 1);
					sh->jid = g_strdup(from);
					js->bs_proxies = g_list_prepend(js->bs_proxies, sh);

					iq = jabber_iq_new_query(js, JABBER_IQ_GET,
							NS_BYTESTREAMS);
					xmlnode_set_attrib(iq->node, "to", sh->jid);
					jabber_iq_set_callback(iq, jabber_disco_bytestream_server_cb, sh);
					jabber_iq_send(iq);
				}

			} else if(purple_strequal(child->name, "feature")) {
				const char *var = xmlnode_get_attrib(child, "var");
				if(!var)
					continue;

				if(purple_strequal(var, "http://jabber.org/protocol/si"))
					capabilities |= JABBER_CAP_SI;
				else if(purple_strequal(var, "http://jabber.org/protocol/si/profile/file-transfer"))
					capabilities |= JABBER_CAP_SI_FILE_XFER;
				else if(purple_strequal(var, NS_BYTESTREAMS))
					capabilities |= JABBER_CAP_BYTESTREAMS;
				else if(purple_strequal(var, "jabber:iq:search"))
					capabilities |= JABBER_CAP_IQ_SEARCH;
				else if(purple_strequal(var, "jabber:iq:register"))
					capabilities |= JABBER_CAP_IQ_REGISTER;
				else if(purple_strequal(var, NS_PING))
					capabilities |= JABBER_CAP_PING;
				else if(purple_strequal(var, NS_DISCO_ITEMS))
					capabilities |= JABBER_CAP_ITEMS;
				else if(purple_strequal(var, "http://jabber.org/protocol/commands")) {
					capabilities |= JABBER_CAP_ADHOC;
				}
				else if(purple_strequal(var, NS_IBB)) {
					purple_debug_info("xmpp2", "remote supports IBB\n");
					capabilities |= JABBER_CAP_IBB;
				}
			}
		}

		js->chat_servers = g_list_reverse(js->chat_servers);

		capabilities |= JABBER_CAP_RETRIEVED;

		if(jbr)
			jbr->capabilities = capabilities;

		/* Only our own server's components (MUC, upload, ...): a foreign
		 * domain advertising upload or carbons must not be used */
		if (x2_ext_is_server_item(js, from))
			x2_ext_server_info(js, from, query);

		if (jdicd && jdicd->callback)
			jdicd->callback(js, from, capabilities, jdicd->data);
	} else { /* type == JABBER_IQ_ERROR or query == NULL */
		JabberID *jid;
		JabberBuddy *jb;
		JabberBuddyResource *jbr = NULL;
		JabberCapabilities capabilities = JABBER_CAP_NONE;

		if((jid = jabber_id_new(from))) {
			if(jid->resource && (jb = jabber_buddy_find(js, from, TRUE)))
				jbr = jabber_buddy_find_resource(jb, jid->resource);
			jabber_id_free(jid);
		}

		if(jbr)
			capabilities = jbr->capabilities;

		if (jdicd && jdicd->callback)
			jdicd->callback(js, from, capabilities, jdicd->data);
	}

	g_free(jdicd);
}

void jabber_disco_items_parse(JabberStream *js, const char *from,
                              JabberIqType type, const char *id,
                              xmlnode *query)
{
	if(type == JABBER_IQ_GET) {
		JabberIq *iq = jabber_iq_new_query(js, JABBER_IQ_RESULT,
				NS_DISCO_ITEMS);

		/* preserve node */
		xmlnode *iq_query = xmlnode_get_child(iq->node, "query");
		const char *node = xmlnode_get_attrib(query, "node");
		if(node)
			xmlnode_set_attrib(iq_query,"node",node);

		jabber_iq_set_id(iq, id);

		if (from)
			xmlnode_set_attrib(iq->node, "to", from);
		jabber_iq_send(iq);
	}
}

/*
 * disco#info of our own bare JID: the account's PEP service and, on servers
 * like Prosody, the account archive features (MAM, carbons) are advertised
 * here and not on the domain.  The roster request follows, whatever the
 * outcome, so the login chain is: server info, account info, roster,
 * pre_presence, initial presence, session_ready.
 */
static void
jabber_disco_account_info_cb(JabberStream *js, const char *from,
                             JabberIqType type, const char *id,
                             xmlnode *packet, gpointer data)
{
	xmlnode *query = xmlnode_get_child_with_namespace(packet, "query",
	                                                  NS_DISCO_INFO);
	char *bare = jabber_id_get_bare_jid(js->user);

	/* The result of a request to our own bare JID may come without "from" */
	if (type == JABBER_IQ_RESULT && query &&
	    (from == NULL || purple_strequal(from, bare)))
		x2_ext_server_info(js, bare, query);
	else
		purple_debug_info("xmpp2", "no usable account disco#info\n");

	g_free(bare);
	jabber_roster_request(js);
}

static void
jabber_disco_account_info(JabberStream *js)
{
	JabberIq *iq = jabber_iq_new_query(js, JABBER_IQ_GET, NS_DISCO_INFO);
	char *bare = jabber_id_get_bare_jid(js->user);

	xmlnode_set_attrib(iq->node, "to", bare);
	g_free(bare);
	jabber_iq_set_callback(iq, jabber_disco_account_info_cb, NULL);
	jabber_iq_send(iq);
}

static void
jabber_disco_finish_server_info_result_cb(JabberStream *js)
{
	const char *ft_proxies;

	/*
	 * This *should* happen only if the server supports vcard-temp, but there
	 * are apparently some servers that don't advertise it even though they
	 * support it.
	 */
	jabber_vcard_fetch_mine(js);

	if (js->pep)
		jabber_avatar_fetch_mine(js);

	/* Yes, please! The roster follows the own account's disco#info. */
	jabber_disco_account_info(js);

	if (js->server_caps & JABBER_CAP_ADHOC) {
		/* The server supports ad-hoc commands, so let's request the list */
		jabber_adhoc_server_get_list(js);
	}

	/* If the server supports blocking, request the block list */
	if (js->server_caps & JABBER_CAP_BLOCKING) {
		jabber_request_block_list(js);
	}

	/* If there are manually specified bytestream proxies, query them */
	ft_proxies = purple_account_get_string(js->gc->account, "ft_proxies", NULL);
	if (ft_proxies) {
		JabberIq *iq;
		JabberBytestreamsStreamhost *sh;
		int i;
		char *tmp;
		gchar **ft_proxy_list = g_strsplit(ft_proxies, ",", 0);

		for(i = 0; ft_proxy_list[i]; i++) {
			g_strstrip(ft_proxy_list[i]);
			if(!(*ft_proxy_list[i]))
				continue;

			/* We used to allow specifying a port directly here; get rid of it */
			if((tmp = strchr(ft_proxy_list[i], ':')))
				*tmp = '\0';

			sh = g_new0(JabberBytestreamsStreamhost, 1);
			sh->jid = g_strdup(ft_proxy_list[i]);
			js->bs_proxies = g_list_prepend(js->bs_proxies, sh);

			iq = jabber_iq_new_query(js, JABBER_IQ_GET, NS_BYTESTREAMS);
			xmlnode_set_attrib(iq->node, "to", sh->jid);
			jabber_iq_set_callback(iq, jabber_disco_bytestream_server_cb, sh);
			jabber_iq_send(iq);
		}

		g_strfreev(ft_proxy_list);
	}

}

static void
jabber_disco_server_info_result_cb(JabberStream *js, const char *from,
                                   JabberIqType type, const char *id,
                                   xmlnode *packet, gpointer data)
{
	xmlnode *query, *child;

	if (!from || !purple_strequal(from, js->user->domain)) {
		jabber_disco_finish_server_info_result_cb(js);
		return;
	}

	if (type == JABBER_IQ_ERROR) {
		/* A common way to get here is for the server not to support xmlns http://jabber.org/protocol/disco#info */
		jabber_disco_finish_server_info_result_cb(js);
		return;
	}

	query = xmlnode_get_child(packet, "query");

	if (!query) {
		jabber_disco_finish_server_info_result_cb(js);
		return;
	}

	for (child = xmlnode_get_child(query, "identity"); child;
	     child = xmlnode_get_next_twin(child)) {
		const char *category, *type, *name;
		category = xmlnode_get_attrib(child, "category");
		type = xmlnode_get_attrib(child, "type");
		if(purple_strequal(category, "pubsub") && purple_strequal(type, "pep")) {
			PurpleConnection *gc = js->gc;
			js->pep = TRUE;
			gc->flags |= PURPLE_CONNECTION_SUPPORT_MOODS |
				PURPLE_CONNECTION_SUPPORT_MOOD_MESSAGES;
		}
		if (!purple_strequal(category, "server"))
			continue;
		if (!purple_strequal(type, "im"))
			continue;

		name = xmlnode_get_attrib(child, "name");
		if (!name)
			continue;

		g_free(js->server_name);
		js->server_name = g_strdup(name);
	}

	for (child = xmlnode_get_child(query, "feature"); child;
	     child = xmlnode_get_next_twin(child)) {
		const char *var;
		var = xmlnode_get_attrib(child, "var");
		if (!var)
			continue;

		if (purple_strequal("http://jabber.org/protocol/commands", var)) {
			js->server_caps |= JABBER_CAP_ADHOC;
		} else if (purple_strequal(NS_SIMPLE_BLOCKING, var)) {
			js->server_caps |= JABBER_CAP_BLOCKING;
		}
	}

	x2_ext_server_info(js, from, query);

	jabber_disco_finish_server_info_result_cb(js);
}

static void
jabber_disco_server_items_result_cb(JabberStream *js, const char *from,
                                    JabberIqType type, const char *id,
                                    xmlnode *packet, gpointer data)
{
	xmlnode *query, *child;

	if (!from || !purple_strequal(from, js->user->domain))
		return;

	if (type == JABBER_IQ_ERROR)
		return;

	while(js->chat_servers) {
		g_free(js->chat_servers->data);
		js->chat_servers = g_list_delete_link(js->chat_servers, js->chat_servers);
	}

	query = xmlnode_get_child(packet, "query");

	for(child = xmlnode_get_child(query, "item"); child;
			child = xmlnode_get_next_twin(child)) {
		JabberIq *iq;
		const char *jid;

		if(!(jid = xmlnode_get_attrib(child, "jid")))
			continue;

		/* we don't actually care about the specific nodes,
		 * so we won't query them */
		if(xmlnode_get_attrib(child, "node") != NULL)
			continue;

		/* trusted source for x2_ext_server_info(), see ext.h */
		x2_ext_server_item_add(js, jid);

		iq = jabber_iq_new_query(js, JABBER_IQ_GET, NS_DISCO_INFO);
		xmlnode_set_attrib(iq->node, "to", jid);
		jabber_iq_set_callback(iq, jabber_disco_info_cb, NULL);
		jabber_iq_send(iq);
	}
}

void jabber_disco_items_server(JabberStream *js)
{
	JabberIq *iq = jabber_iq_new_query(js, JABBER_IQ_GET, NS_DISCO_ITEMS);

	xmlnode_set_attrib(iq->node, "to", js->user->domain);

	jabber_iq_set_callback(iq, jabber_disco_server_items_result_cb, NULL);
	jabber_iq_send(iq);

	iq = jabber_iq_new_query(js, JABBER_IQ_GET, NS_DISCO_INFO);
	xmlnode_set_attrib(iq->node, "to", js->user->domain);
	jabber_iq_set_callback(iq, jabber_disco_server_info_result_cb, NULL);
	jabber_iq_send(iq);
}

void jabber_disco_info_do(JabberStream *js, const char *who, JabberDiscoInfoCallback *callback, gpointer data)
{
	JabberID *jid;
	JabberBuddy *jb;
	JabberBuddyResource *jbr = NULL;
	struct _jabber_disco_info_cb_data *jdicd;
	JabberIq *iq;

	if((jid = jabber_id_new(who))) {
		if(jid->resource && (jb = jabber_buddy_find(js, who, TRUE)))
			jbr = jabber_buddy_find_resource(jb, jid->resource);
		jabber_id_free(jid);
	}

	if(jbr && jbr->capabilities & JABBER_CAP_RETRIEVED) {
		callback(js, who, jbr->capabilities, data);
		return;
	}

	jdicd = g_new0(struct _jabber_disco_info_cb_data, 1);
	jdicd->data = data;
	jdicd->callback = callback;

	iq = jabber_iq_new_query(js, JABBER_IQ_GET, NS_DISCO_INFO);
	xmlnode_set_attrib(iq->node, "to", who);

	jabber_iq_set_callback(iq, jabber_disco_info_cb, jdicd);
	jabber_iq_send(iq);
}

