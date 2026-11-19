/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/*
 * Unit tests of the X2Msg pipeline (ext/ext.c, ext/msgctx.c): context
 * building, stanza-id trust, X2_DEFERRED ownership (dup and redeliver),
 * trusted disco sources and per-module state slots.
 *
 * ext.c is built with X2_EXT_TEST_TABLE=x2_test_exts, so the registry is the
 * table below.  Delivery and the room lookup are stubbed; everything else
 * is the real code.  Run under valgrind or XMPP2_SANITIZE to check that
 * deferred copies neither leak nor touch the freed parser stanza.
 */
#include "x2test.h"

#include "chat.h"
#include "ext/ext.h"
#include "ext/msgctx.h"
#include "jutil.h"
#include "message.h"

#define ME "me@example.org"

/* ---- stubs of the inherited code ---- */

static struct {
	int count;
	char *body;
	char *peer;
	time_t ts;
	gboolean delayed;
	X2Source src;
} delivered;

void
jabber_message_deliver(JabberStream *js, X2Msg *ctx)
{
	g_assert_nonnull(js);
	delivered.count++;
	g_free(delivered.body);
	delivered.body = xmlnode_get_data(xmlnode_get_child(ctx->msg, "body"));
	g_free(delivered.peer);
	delivered.peer = g_strdup(ctx->peer);
	delivered.ts = ctx->ts;
	delivered.delayed = ctx->delayed;
	delivered.src = ctx->src;
}

JabberChat *
jabber_chat_find(JabberStream *js, const char *room, const char *server)
{
	return NULL;
}

/* ---- test modules ---- */

static gboolean defer_next;
static X2Msg *deferred;
static int seen_defer, seen_count;

static const X2Ext mod_defer;

static void
defer_stream_new(JabberStream *js)
{
	x2_ext_set_priv(js, &mod_defer, g_strdup("defer-state"));
}

static void
defer_stream_free(JabberStream *js)
{
	g_free(x2_ext_get_priv(js, &mod_defer));
	x2_ext_set_priv(js, &mod_defer, NULL);
}

/* Unwraps a sent carbon like the carbons module will, then defers it */
static X2Verdict
defer_inbound(JabberStream *js, X2Msg *m)
{
	xmlnode *sent = xmlnode_get_child(m->outer, "sent");

	seen_defer++;
	if (sent != NULL) {
		xmlnode *fwd = xmlnode_get_child(sent, "forwarded");

		x2_msg_set_inner(js, m, xmlnode_get_child(fwd, "message"),
		                 X2_SRC_CARBON, X2_DIR_OUT, NULL);
	}
	if (!defer_next)
		return X2_PASS;
	g_assert_null(deferred);
	deferred = x2_msg_dup(m);
	g_assert_nonnull(deferred);
	return X2_DEFERRED;
}

static X2Verdict
count_inbound(JabberStream *js, X2Msg *m)
{
	seen_count++;
	return X2_PASS;
}

static const X2Ext mod_defer = {
	.name = "defer",
	.stream_new = defer_stream_new,
	.stream_free = defer_stream_free,
	.inbound = defer_inbound,
};

static const X2Ext mod_count = {
	.name = "count",
	.inbound = count_inbound,
};

static const X2Ext mod_unregistered = { .name = "unregistered" };

const X2Ext *const x2_test_exts[] = { &mod_defer, &mod_count, NULL };

/* ---- fixture ---- */

typedef struct {
	JabberStream *js;
} Fixture;

static void
setup(Fixture *f, gconstpointer data)
{
	f->js = g_new0(JabberStream, 1);
	f->js->user = jabber_id_new(ME "/desk");
	x2_ext_stream_new(f->js);
	memset(&delivered, 0, sizeof(delivered));
	defer_next = FALSE;
	deferred = NULL;
	seen_defer = seen_count = 0;
}

static void
teardown(Fixture *f, gconstpointer data)
{
	x2_ext_stream_free(f->js);
	g_assert_null(f->js->x2);
	jabber_id_free(f->js->user);
	g_free(f->js);
	g_free(delivered.body);
	g_free(delivered.peer);
	g_assert_null(deferred);
}

static xmlnode *
parse(const char *xml)
{
	xmlnode *n = xmlnode_from_str(xml, -1);

	g_assert_nonnull(n);
	return n;
}

static xmlnode *
disco(const char *feature)
{
	char *xml = g_strdup_printf(
		"<query xmlns='http://jabber.org/protocol/disco#info'>"
		"<feature var='%s'/></query>", feature);
	xmlnode *q = parse(xml);

	g_free(xml);
	return q;
}

/* ---- tests ---- */

static void
test_pass_delivers(Fixture *f, gconstpointer data)
{
	xmlnode *p = parse("<message xmlns='jabber:client' from='bob@example.org/p'"
	                   " to='" ME "/desk' type='chat'><body>hi</body></message>");

	x2_msg_receive(f->js, p);
	xmlnode_free(p);

	g_assert_cmpint(seen_defer, ==, 1);
	g_assert_cmpint(seen_count, ==, 1);
	g_assert_cmpint(delivered.count, ==, 1);
	g_assert_cmpstr(delivered.body, ==, "hi");
	g_assert_cmpstr(delivered.peer, ==, "bob@example.org");
	g_assert_false(delivered.delayed);
}

/* The deferred copy must outlive the parser's stanza and resume after the
 * module that deferred it, with msg relocated into the copy. */
static void
test_defer_and_redeliver(Fixture *f, gconstpointer data)
{
	xmlnode *p = parse(
		"<message xmlns='jabber:client' from='" ME "' to='" ME "/desk'>"
		"<sent xmlns='urn:xmpp:carbons:2'>"
		"<forwarded xmlns='urn:xmpp:forward:0'>"
		"<delay xmlns='urn:xmpp:delay' stamp='2024-01-02T03:04:05Z'/>"
		"<message xmlns='jabber:client' from='" ME "/phone'"
		" to='bob@example.org' type='chat'>"
		"<origin-id xmlns='urn:xmpp:sid:0' id='o1'/>"
		"<body>from phone</body></message></forwarded></sent></message>");
	X2Msg *d;

	defer_next = TRUE;
	x2_msg_receive(f->js, p);
	/* the parser frees the stanza right after the pipeline */
	xmlnode_free(p);

	g_assert_cmpint(seen_defer, ==, 1);
	g_assert_cmpint(seen_count, ==, 0);
	g_assert_cmpint(delivered.count, ==, 0);

	d = deferred;
	deferred = NULL;
	g_assert_true(d->owns_outer);
	g_assert_cmpint(d->stage, ==, 0);
	g_assert_true(d->msg != d->outer);
	g_assert_cmpstr(d->key, ==, "O:out:bob@example.org:o1");

	x2_msg_redeliver(f->js, d);

	g_assert_cmpint(seen_defer, ==, 1);   /* not run again */
	g_assert_cmpint(seen_count, ==, 1);
	g_assert_cmpint(delivered.count, ==, 1);
	g_assert_cmpstr(delivered.body, ==, "from phone");
	g_assert_cmpstr(delivered.peer, ==, "bob@example.org");
	g_assert_cmpint(delivered.src, ==, X2_SRC_CARBON);
	/* send time from <forwarded><delay/> */
	g_assert_true(delivered.delayed);
	g_assert_cmpint(delivered.ts, ==, 1704164645);
}

static void
test_redeliver_rejects_borrowed(Fixture *f, gconstpointer data)
{
	xmlnode *p = parse("<message xmlns='jabber:client' from='bob@example.org/p'>"
	                   "<body>x</body></message>");
	X2Msg *m = x2_msg_new(f->js, p);

	g_test_expect_message(NULL, G_LOG_LEVEL_CRITICAL,
	                      "*not a deferred x2_msg_dup() copy*");
	x2_msg_redeliver(f->js, m);
	g_test_assert_expected_messages();
	g_assert_cmpint(delivered.count, ==, 0);
	xmlnode_free(p);
}

static void
test_delay_forwarded_then_inner(Fixture *f, gconstpointer data)
{
	xmlnode *p = parse(
		"<message xmlns='jabber:client' from='" ME "'>"
		"<result xmlns='urn:xmpp:mam:2' id='r1'>"
		"<forwarded xmlns='urn:xmpp:forward:0'>"
		"<message xmlns='jabber:client' from='bob@example.org/p' to='" ME "'"
		" type='chat'>"
		"<delay xmlns='urn:xmpp:delay' stamp='2024-01-01T00:00:00Z'/>"
		"<body>old</body></message></forwarded></result></message>");
	xmlnode *fwd = xmlnode_get_child(xmlnode_get_child(p, "result"),
	                                 "forwarded");
	X2Msg *m = x2_msg_new(f->js, p);
	xmlnode *delay;

	g_assert_false(m->delayed);

	/* no delay on <forwarded/>: the inner one applies */
	x2_msg_set_inner(f->js, m, xmlnode_get_child(fwd, "message"),
	                 X2_SRC_MAM, X2_DIR_IN, ME);
	g_assert_true(m->delayed);
	g_assert_cmpint(m->ts, ==, 1704067200);

	/* with one, it wins */
	delay = xmlnode_new_child(fwd, "delay");
	xmlnode_set_namespace(delay, "urn:xmpp:delay");
	xmlnode_set_attrib(delay, "stamp", "2024-01-02T03:04:05Z");
	x2_msg_set_inner(f->js, m, xmlnode_get_child(fwd, "message"),
	                 X2_SRC_MAM, X2_DIR_IN, ME);
	g_assert_cmpint(m->ts, ==, 1704164645);

	x2_msg_free(m);
	xmlnode_free(p);
}

static void
test_live_stanza_id_trust(Fixture *f, gconstpointer data)
{
	const char *xml =
		"<message xmlns='jabber:client' from='bob@example.org/p'"
		" to='" ME "/desk' type='chat'>"
		"<stanza-id xmlns='urn:xmpp:sid:0' by='" ME "' id='s1'/>"
		"<origin-id xmlns='urn:xmpp:sid:0' id='o1'/>"
		"<body>hi</body></message>";
	xmlnode *p = parse(xml);
	xmlnode *q;
	X2Msg *m;

	/* our archive has not advertised sid/mam: a forged by=me is ignored */
	m = x2_msg_new(f->js, p);
	g_assert_cmpstr(m->archive, ==, ME);
	g_assert_null(m->stanza_id);
	g_assert_cmpstr(m->key, ==, "O:in:bob@example.org:o1");
	x2_msg_free(m);

	/* a foreign entity is refused before any module sees it */
	q = disco("urn:xmpp:mam:2");
	g_test_expect_message(NULL, G_LOG_LEVEL_CRITICAL,
	                      "*untrusted source evil.example.net*");
	x2_ext_server_info(f->js, "evil.example.net", q);
	g_test_assert_expected_messages();
	/* a server item advertising MAM is not our archive either */
	x2_ext_server_item_add(f->js, "conf.example.org");
	x2_ext_server_info(f->js, "conf.example.org", q);
	m = x2_msg_new(f->js, p);
	g_assert_null(m->stanza_id);
	x2_msg_free(m);

	/* our own account disco does */
	x2_ext_server_info(f->js, ME, q);
	xmlnode_free(q);
	m = x2_msg_new(f->js, p);
	g_assert_cmpstr(m->stanza_id, ==, "s1");
	g_assert_cmpstr(m->key, ==, "S:s1");
	x2_msg_free(m);
	xmlnode_free(p);

	/* urn:xmpp:sid:0 alone is enough; by= must still match the archive */
	x2_ext_stream_free(f->js);
	x2_ext_stream_new(f->js);
	q = disco("urn:xmpp:sid:0");
	x2_ext_server_info(f->js, ME, q);
	xmlnode_free(q);
	p = parse("<message xmlns='jabber:client' from='bob@example.org/p'>"
	          "<stanza-id xmlns='urn:xmpp:sid:0' by='bob@example.org' id='s2'/>"
	          "<body>hi</body></message>");
	m = x2_msg_new(f->js, p);
	g_assert_null(m->stanza_id);
	g_assert_null(m->key);   /* no origin-id, and @id is never a key */
	x2_msg_free(m);
	xmlnode_free(p);
}

static void
test_groupchat_archive_is_room(Fixture *f, gconstpointer data)
{
	xmlnode *p = parse(
		"<message xmlns='jabber:client' from='room@conf.example.org/nick'"
		" type='groupchat'>"
		"<stanza-id xmlns='urn:xmpp:sid:0' by='room@conf.example.org' id='g1'/>"
		"<body>hi</body></message>");
	X2Msg *m = x2_msg_new(f->js, p);

	g_assert_cmpstr(m->archive, ==, "room@conf.example.org");
	g_assert_cmpstr(m->peer, ==, "room@conf.example.org");
	g_assert_null(m->stanza_id);
	x2_msg_free(m);

	/* after the room's disco (M4) */
	x2_ext_archive_trust(f->js, "room@conf.example.org");
	m = x2_msg_new(f->js, p);
	g_assert_cmpstr(m->stanza_id, ==, "g1");
	x2_msg_free(m);
	xmlnode_free(p);
}

static void
test_muc_pm_hint(Fixture *f, gconstpointer data)
{
	xmlnode *pm = parse(
		"<message xmlns='jabber:client' from='room@conf.example.org/nick'"
		" type='chat'><x xmlns='http://jabber.org/protocol/muc#user'/>"
		"<body>psst</body></message>");
	xmlnode *plain = parse(
		"<message xmlns='jabber:client' from='bob@example.org/laptop'"
		" type='chat'><body>hi</body></message>");
	X2Msg *m;

	/* not joined (jabber_chat_find() is NULL), the hint is enough */
	m = x2_msg_new(f->js, pm);
	g_assert_cmpstr(m->peer, ==, "room@conf.example.org/nick");
	x2_msg_free(m);

	m = x2_msg_new(f->js, plain);
	g_assert_cmpstr(m->peer, ==, "bob@example.org");
	x2_msg_free(m);

	xmlnode_free(pm);
	xmlnode_free(plain);
}

static void
test_server_items(Fixture *f, gconstpointer data)
{
	g_assert_true(x2_ext_is_server_item(f->js, "example.org"));
	g_assert_false(x2_ext_is_server_item(f->js, "upload.example.org"));
	g_assert_false(x2_ext_is_server_item(f->js, NULL));

	x2_ext_server_item_add(f->js, "upload.example.org");
	g_assert_true(x2_ext_is_server_item(f->js, "upload.example.org"));
	g_assert_false(x2_ext_is_server_item(f->js, "upload.evil.example.net"));
}

static void
test_priv_slots(Fixture *f, gconstpointer data)
{
	g_assert_cmpstr(x2_ext_get_priv(f->js, &mod_defer), ==, "defer-state");
	g_assert_null(x2_ext_get_priv(f->js, &mod_count));

	g_test_expect_message(NULL, G_LOG_LEVEL_CRITICAL, "*i >= 0*");
	g_assert_null(x2_ext_get_priv(f->js, &mod_unregistered));
	g_test_assert_expected_messages();
}

#define ADD(path, fn) \
	g_test_add(path, Fixture, NULL, setup, fn, teardown)

int
main(int argc, char **argv)
{
	x2_test_init(&argc, &argv);
	ADD("/msgctx/pass_delivers", test_pass_delivers);
	ADD("/msgctx/defer_and_redeliver", test_defer_and_redeliver);
	ADD("/msgctx/redeliver_rejects_borrowed", test_redeliver_rejects_borrowed);
	ADD("/msgctx/delay_forwarded_then_inner", test_delay_forwarded_then_inner);
	ADD("/msgctx/live_stanza_id_trust", test_live_stanza_id_trust);
	ADD("/msgctx/groupchat_archive_is_room", test_groupchat_archive_is_room);
	ADD("/msgctx/muc_pm_hint", test_muc_pm_hint);
	ADD("/msgctx/server_items", test_server_items);
	ADD("/msgctx/priv_slots", test_priv_slots);
	return g_test_run();
}
