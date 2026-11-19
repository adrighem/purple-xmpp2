/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 purple-xmpp2 contributors
 */
/* Unit tests of core/carbon_parse.c: classification and forged carbons. */
#include <glib.h>

#include "x2test.h"

#include "carbon_parse.h"

#define ME "alice@localhost"

static xmlnode *
parse(const char *xml)
{
	xmlnode *n = xmlnode_from_str(xml, -1);

	g_assert_nonnull(n);
	return n;
}

/*
 * A carbon.  outer_from NULL means no "from" attribute; wrapper is "sent" or
 * "received"; inner_attrs are the attributes of the wrapped message.
 */
static xmlnode *
carbon(const char *outer_from, const char *wrapper, const char *inner_attrs)
{
	char *from = outer_from ? g_strdup_printf(" from='%s'", outer_from)
	                        : g_strdup("");
	char *xml = g_strdup_printf(
		"<message xmlns='jabber:client'%s to='alice@localhost/laptop'>"
		"<%s xmlns='urn:xmpp:carbons:2'>"
		"<forwarded xmlns='urn:xmpp:forward:0'>"
		"<message xmlns='jabber:client' %s><body>hi</body></message>"
		"</forwarded></%s></message>",
		from, wrapper, inner_attrs, wrapper);
	xmlnode *n = parse(xml);

	g_free(from);
	g_free(xml);
	return n;
}

/* Classifies, checks kind (and reason for REJECTED), frees outer */
static void
expect_kind(xmlnode *outer, X2CarbonKind want, const char *want_reason)
{
	xmlnode *inner = (xmlnode *)0x1;
	const char *reason = (const char *)0x1;
	X2CarbonKind got = x2_carbon_classify(outer, ME, &inner, &reason);

	g_assert_cmpint(got, ==, want);
	if (want == X2_CARBON_SENT || want == X2_CARBON_RECEIVED)
		g_assert_nonnull(inner);
	else
		g_assert_null(inner);
	if (want == X2_CARBON_REJECTED)
		g_assert_cmpstr(reason, ==, want_reason);
	else
		g_assert_null(reason);
	xmlnode_free(outer);
}

static void
test_valid_sent(void)
{
	xmlnode *outer = carbon(ME, "sent",
		"from='alice@localhost/phone' to='bob@localhost/x' type='chat'");
	xmlnode *inner = NULL;

	g_assert_cmpint(x2_carbon_classify(outer, ME, &inner, NULL), ==,
	                X2_CARBON_SENT);
	g_assert_cmpstr(xmlnode_get_attrib(inner, "to"), ==, "bob@localhost/x");
	xmlnode_free(outer);
}

static void
test_valid_received(void)
{
	xmlnode *outer = carbon(ME, "received",
		"from='bob@localhost/x' to='alice@localhost/phone' type='chat'");
	xmlnode *inner = NULL;

	g_assert_cmpint(x2_carbon_classify(outer, ME, &inner, NULL), ==,
	                X2_CARBON_RECEIVED);
	g_assert_cmpstr(xmlnode_get_attrib(inner, "from"), ==, "bob@localhost/x");
	xmlnode_free(outer);
}

static void
test_outer_from_absent_is_accepted(void)
{
	expect_kind(carbon(NULL, "sent",
		"from='alice@localhost/phone' to='bob@localhost'"),
		X2_CARBON_SENT, NULL);
	expect_kind(carbon(NULL, "received",
		"from='bob@localhost' to='alice@localhost/phone'"),
		X2_CARBON_RECEIVED, NULL);
}

static void
test_normal_inner(void)
{
	expect_kind(carbon(ME, "sent",
		"from='alice@localhost/p' to='bob@localhost' type='normal'"),
		X2_CARBON_SENT, NULL);
}

/* Forged case 1: a contact wraps a <sent/> or <received/> */
static void
test_forged_outer_from_contact(void)
{
	expect_kind(carbon("bob@localhost", "sent",
		"from='alice@localhost/phone' to='bob@localhost' type='chat'"),
		X2_CARBON_REJECTED, "outer from is not our bare JID");
	expect_kind(carbon("bob@localhost", "received",
		"from='bob@localhost' to='alice@localhost' type='chat'"),
		X2_CARBON_REJECTED, "outer from is not our bare JID");
}

/* Our own full JID or a look-alike is not our bare JID */
static void
test_forged_outer_from_variants(void)
{
	const char *bad[] = {
		"alice@localhost/phone", "alice@localhost.evil",
		"alice@localhostx", "xalice@localhost", "localhost", "", NULL
	};
	int i;

	for (i = 0; bad[i]; i++)
		expect_kind(carbon(bad[i], "sent",
			"from='alice@localhost/p' to='bob@localhost'"),
			X2_CARBON_REJECTED, "outer from is not our bare JID");
}

/* Forged case 2: right outer from, <sent/> whose inner from is foreign */
static void
test_forged_sent_inner_from(void)
{
	expect_kind(carbon(ME, "sent",
		"from='bob@localhost/x' to='carol@localhost' type='chat'"),
		X2_CARBON_REJECTED, "sent carbon inner from is not our JID");
	expect_kind(carbon(ME, "sent",
		"from='alice@localhost.evil/p' to='carol@localhost'"),
		X2_CARBON_REJECTED, "sent carbon inner from is not our JID");
	expect_kind(carbon(ME, "sent", "to='carol@localhost'"),
		X2_CARBON_REJECTED, "sent carbon inner from is not our JID");
}

/* Forged case 3: right outer from, <received/> whose inner to is foreign */
static void
test_forged_received_inner_to(void)
{
	expect_kind(carbon(ME, "received",
		"from='bob@localhost/x' to='carol@localhost' type='chat'"),
		X2_CARBON_REJECTED, "received carbon inner to is not our JID");
	expect_kind(carbon(ME, "received", "from='bob@localhost'"),
		X2_CARBON_REJECTED, "received carbon inner to is not our JID");
}

static void
test_missing_peer(void)
{
	expect_kind(carbon(ME, "sent", "from='alice@localhost/p'"),
		X2_CARBON_REJECTED, "inner message has no peer JID");
	expect_kind(carbon(ME, "received", "to='alice@localhost/p'"),
		X2_CARBON_REJECTED, "inner message has no peer JID");
}

static void
test_inner_type(void)
{
	expect_kind(carbon(ME, "sent",
		"from='alice@localhost/p' to='room@conf.localhost' "
		"type='groupchat'"),
		X2_CARBON_REJECTED, "inner message type not chat or normal");
	expect_kind(carbon(ME, "received",
		"from='bob@localhost' to='alice@localhost' type='error'"),
		X2_CARBON_REJECTED, "inner message type not chat or normal");
}

static void
test_no_forwarded_message(void)
{
	expect_kind(parse(
		"<message xmlns='jabber:client' from='alice@localhost'>"
		"<sent xmlns='urn:xmpp:carbons:2'/></message>"),
		X2_CARBON_REJECTED, "no forwarded message");
	expect_kind(parse(
		"<message xmlns='jabber:client' from='alice@localhost'>"
		"<sent xmlns='urn:xmpp:carbons:2'>"
		"<forwarded xmlns='urn:other'><message from='alice@localhost/p' "
		"to='bob@localhost'/></forwarded></sent></message>"),
		X2_CARBON_REJECTED, "no forwarded message");
}

static void
test_ambiguous_wrapper(void)
{
	expect_kind(parse(
		"<message xmlns='jabber:client' from='alice@localhost'>"
		"<sent xmlns='urn:xmpp:carbons:2'><forwarded "
		"xmlns='urn:xmpp:forward:0'><message from='alice@localhost/p' "
		"to='bob@localhost'/></forwarded></sent>"
		"<received xmlns='urn:xmpp:carbons:2'><forwarded "
		"xmlns='urn:xmpp:forward:0'><message from='bob@localhost' "
		"to='alice@localhost'/></forwarded></received></message>"),
		X2_CARBON_REJECTED, "ambiguous carbon wrapper");
}

static void
test_not_a_carbon(void)
{
	expect_kind(parse(
		"<message xmlns='jabber:client' from='bob@localhost/x' "
		"type='chat'><body>hi</body></message>"), X2_CARBON_NONE, NULL);
	/* wrong namespace: just a funny child, not a carbon */
	expect_kind(parse(
		"<message xmlns='jabber:client' from='bob@localhost/x'>"
		"<sent xmlns='urn:other'/></message>"), X2_CARBON_NONE, NULL);
	/* the private hint is not a carbon either */
	expect_kind(parse(
		"<message xmlns='jabber:client' from='bob@localhost/x'>"
		"<private xmlns='urn:xmpp:carbons:2'/></message>"),
		X2_CARBON_NONE, NULL);
}

static void
test_bare_jid_case_insensitive(void)
{
	expect_kind(carbon("Alice@LocalHost", "sent",
		"from='ALICE@localhost/p' to='bob@localhost'"),
		X2_CARBON_SENT, NULL);
}

int
main(int argc, char **argv)
{
	x2_test_init(&argc, &argv);

	g_test_add_func("/carbon/valid-sent", test_valid_sent);
	g_test_add_func("/carbon/valid-received", test_valid_received);
	g_test_add_func("/carbon/outer-from-absent",
	                test_outer_from_absent_is_accepted);
	g_test_add_func("/carbon/inner-normal", test_normal_inner);
	g_test_add_func("/carbon/forged/outer-from-contact",
	                test_forged_outer_from_contact);
	g_test_add_func("/carbon/forged/outer-from-variants",
	                test_forged_outer_from_variants);
	g_test_add_func("/carbon/forged/sent-inner-from",
	                test_forged_sent_inner_from);
	g_test_add_func("/carbon/forged/received-inner-to",
	                test_forged_received_inner_to);
	g_test_add_func("/carbon/missing-peer", test_missing_peer);
	g_test_add_func("/carbon/inner-type", test_inner_type);
	g_test_add_func("/carbon/no-forwarded", test_no_forwarded_message);
	g_test_add_func("/carbon/ambiguous", test_ambiguous_wrapper);
	g_test_add_func("/carbon/not-a-carbon", test_not_a_carbon);
	g_test_add_func("/carbon/case-insensitive",
	                test_bare_jid_case_insensitive);
	return g_test_run();
}
