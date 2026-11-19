#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# M8: copy of prpl-jabber accounts to prpl-xmpp2 (PLAN M8 acceptance).
# Run: PRPL=prpl-xmpp2 PLUGIN_DIR=build tests/integration/test_migrate.sh
#
# The userdirs are seeded with prpl-jabber accounts and a buddy list; the
# harness account (carol, prpl-xmpp2) runs the prpl action or answers the
# startup offer.  accounts.xml, blist.xml, prefs.xml and the state files are
# checked afterwards.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$HERE/lib.sh"
[[ "$PRPL" == "prpl-xmpp2" ]] || { echo "SKIP: needs PRPL=prpl-xmpp2"; exit 77; }
test_init migrate

UI=xmpp2harness
# the hook that offers in any UI exists only in -DXMPP2_TEST_HOOKS=ON builds
ANY_UI=(--libpref b:/plugins/prpl/xmpp2/migration_offer_any_ui=1)

# seed DIR: alice@localhost/laptop (enabled, saved password, alias, options,
# global proxy) and bob@localhost/orig2 (disabled, no saved password, own
# proxy), a buddy list with one group per use, and an old log of alice.
seed() {
	local dir="$1"
	mkdir -p "$dir"
	cat >"$dir/accounts.xml" <<XML
<?xml version='1.0' encoding='UTF-8' ?>

<account version='1.0'>
	<account>
		<protocol>prpl-jabber</protocol>
		<name>alice@localhost/laptop</name>
		<password>$XMPP_PASSWORD</password>
		<alias>Ally</alias>
		<settings>
			<setting name='connection_security' type='string'>opportunistic_tls</setting>
			<setting name='auth_plain_in_clear' type='bool'>1</setting>
			<setting name='port' type='int'>$XMPP_C2S_PORT</setting>
			<setting name='connect_server' type='string'>127.0.0.1</setting>
			<setting name='ft_proxies' type='string'>proxy.localhost</setting>
			<setting name='unknown_to_xmpp2' type='string'>x</setting>
		</settings>
		<settings ui='$UI'>
			<setting name='auto-login' type='bool'>1</setting>
		</settings>
	</account>
	<account>
		<protocol>prpl-jabber</protocol>
		<name>bob@localhost/orig2</name>
		<settings>
			<setting name='connection_security' type='string'>require_tls</setting>
			<setting name='port' type='int'>5223</setting>
		</settings>
		<proxy>
			<type>http</type>
			<host>proxy.example</host>
			<port>3128</port>
			<username>pu</username>
			<password>pp</password>
		</proxy>
	</account>
</account>
XML
	cat >"$dir/blist.xml" <<'XML'
<?xml version='1.0' encoding='UTF-8' ?>

<purple version='1.0'>
	<blist>
		<group name='Friends'>
			<contact>
				<buddy account='alice@localhost/laptop' proto='prpl-jabber'>
					<name>bob@localhost</name>
					<alias>Bobby</alias>
				</buddy>
			</contact>
			<contact>
				<buddy account='alice@localhost/laptop' proto='prpl-jabber'>
					<name>carol@localhost</name>
				</buddy>
			</contact>
			<chat proto='prpl-jabber' account='alice@localhost/laptop'>
				<component name='room'>room1</component>
				<component name='server'>conference.localhost</component>
				<component name='handle'>alice</component>
				<alias>Room One</alias>
			</chat>
		</group>
		<group name='Work'>
			<contact>
				<buddy account='bob@localhost/orig2' proto='prpl-jabber'>
					<name>alice@localhost</name>
				</buddy>
			</contact>
			<chat proto='prpl-jabber' account='bob@localhost/orig2'>
				<component name='room'>dev</component>
				<component name='server'>conference.localhost</component>
				<component name='handle'>bob</component>
				<setting name='gtk-autojoin' type='bool'>1</setting>
			</chat>
		</group>
	</blist>
	<privacy>
		<account proto='prpl-jabber' name='alice@localhost/laptop' mode='1'/>
	</privacy>
</purple>
XML
}

# dump_accounts FILE: one "ACCT proto name enabled=.. pw=.. alias=.." line per
# account and one "  set k=v" / "  proxy ..." line per detail.
dump_accounts() {
	python3 -I - "$1" "$UI" <<'PY'
import sys
import xml.etree.ElementTree as ET

root = ET.parse(sys.argv[1]).getroot()
ui = sys.argv[2]
for a in root.findall("account"):
    def txt(tag):
        e = a.find(tag)
        return e.text if e is not None and e.text else "-"
    enabled = 0
    for s in a.findall("settings"):
        if s.get("ui") == ui:
            for st in s.findall("setting"):
                if st.get("name") == "auto-login":
                    enabled = int(st.text or 0)
    print("ACCT %s %s enabled=%d pw=%s alias=%s" % (
        txt("protocol"), txt("name"), enabled, txt("password"), txt("alias")))
    for s in a.findall("settings"):
        if s.get("ui"):
            continue
        for st in s.findall("setting"):
            print("  set %s=%s" % (st.get("name"), st.text))
    p = a.find("proxy")
    if p is not None:
        print("  proxy %s %s:%s %s/%s" % (
            p.findtext("type"), p.findtext("host"), p.findtext("port"),
            p.findtext("username"), p.findtext("password")))
PY
}

# dump_blist FILE: "BUDDY proto account group name alias" and
# "CHAT proto account group alias room server handle autojoin" lines.
dump_blist() {
	python3 -I - "$1" <<'PY'
import sys
import xml.etree.ElementTree as ET

root = ET.parse(sys.argv[1]).getroot()
for g in root.find("blist").findall("group"):
    for c in g.iter("buddy"):
        print("BUDDY %s %s %s %s %s" % (
            c.get("proto"), c.get("account"), g.get("name"),
            c.findtext("name"), c.findtext("alias") or "-"))
    for c in g.findall("chat"):
        comp = {x.get("name"): x.text for x in c.findall("component")}
        auto = [s.text for s in c.findall("setting")
                if s.get("name") == "gtk-autojoin"]
        print("CHAT %s %s %s %s %s %s %s %s" % (
            c.get("proto"), c.get("account"), g.get("name"),
            c.findtext("alias") or "-", comp.get("room"),
            comp.get("server"), comp.get("handle"), auto[0] if auto else "0"))
PY
}

# has FILE LINE: the dump contains LINE exactly
has() { grep -qxF -- "$2" "$1" || { cat "$1" >&2; fail "missing: $2"; }; }
lacks() { ! grep -qxF -- "$2" "$1" || { cat "$1" >&2; fail "unexpected: $2"; }; }

# --- 1. the prpl action: copy, login, history, logs --------------------------
HUSERDIR="$WORK/ud-action"
export HUSERDIR HLOG=txt
seed "$HUSERDIR"
# an old log written by prpl-jabber (libpurple names the directory after the
# normalised username: the bare JID)
OLDLOG="$HUSERDIR/logs/jabber/alice@localhost/bob@localhost"
mkdir -p "$OLDLOG"
echo "(10:00:00) bob@localhost: from-the-old-days" \
	>"$OLDLOG/2026-01-01.100000+0000UTC.txt"

# hist1 sits in the archive; our second device takes the offline copy, so the
# only thing left that could show it is a MAM replay.
run_peer bob --do "send alice@localhost hist1" --do "sleep 1" >/dev/null
PEER_RESOURCE=phone run_peer alice --do "wait hist1 10" >/dev/null
PEER_RESOURCE=phone peer_bg "$WORK/bob.log" bob --stay 40 --do "sleep 14" \
	--do "send alice@localhost new1"
BOB_PID=$PEER_PID
run_harness carol --debug <<SCRIPT
connect 30
action Copy XMPP accounts
answer 0
expect-account prpl-xmpp2 alice@localhost/laptop enabled=1 connected=1 timeout=40
expect-account prpl-jabber alice@localhost/laptop enabled=0 connected=0
expect-account prpl-xmpp2 bob@localhost/orig2 enabled=0 connected=0
expect-account prpl-jabber bob@localhost/orig2 enabled=0 connected=0
expect-ev kind=recv conv=bob@localhost timeout=40 texteq=new1
no-ev timeout=1 text=hist1
disconnect
SCRIPT
kill "$BOB_PID" 2>/dev/null || true
grep -q "^REQUEST Copy 2 XMPP accounts to XMPP (xmpp2)? \[Copy|Cancel\]" "$H_OUT" ||
	fail "action did not ask for confirmation"
pass "action asks, copies, enables the clone of an enabled original, logs in, shows no history"

dump_accounts "$HUSERDIR/accounts.xml" >"$WORK/acc.dump"
dump_blist "$HUSERDIR/blist.xml" >"$WORK/bl.dump"
grep -c '^ACCT' "$WORK/acc.dump" | grep -qx 5 || fail "want 5 accounts"

# alice: username with resource, saved password, alias, options, no extras
has "$WORK/acc.dump" "ACCT prpl-xmpp2 alice@localhost/laptop enabled=1 pw=$XMPP_PASSWORD alias=Ally"
has "$WORK/acc.dump" "ACCT prpl-jabber alice@localhost/laptop enabled=0 pw=$XMPP_PASSWORD alias=Ally"
sed -n '/^ACCT prpl-xmpp2 alice/,/^ACCT/p' "$WORK/acc.dump" >"$WORK/alice.clone"
has "$WORK/alice.clone" "  set connection_security=opportunistic_tls"
has "$WORK/alice.clone" "  set auth_plain_in_clear=1"
has "$WORK/alice.clone" "  set port=$XMPP_C2S_PORT"
has "$WORK/alice.clone" "  set connect_server=127.0.0.1"
has "$WORK/alice.clone" "  set ft_proxies=proxy.localhost"
lacks "$WORK/alice.clone" "  set unknown_to_xmpp2=x"
# bob: disabled original, so a disabled clone, no password invented, proxy
has "$WORK/acc.dump" "ACCT prpl-xmpp2 bob@localhost/orig2 enabled=0 pw=- alias=-"
has "$WORK/acc.dump" "ACCT prpl-jabber bob@localhost/orig2 enabled=0 pw=- alias=-"
sed -n '/^ACCT prpl-xmpp2 bob/,/^ACCT/p' "$WORK/acc.dump" >"$WORK/bob.clone"
has "$WORK/bob.clone" "  set connection_security=require_tls"
has "$WORK/bob.clone" "  set port=5223"
has "$WORK/bob.clone" "  proxy http proxy.example:3128 pu/pp"
pass "settings, resource, saved password, alias, proxy copied; disabled original gives disabled clone"

# buddy list: groups, aliases, bookmarks per account, originals untouched
for p in prpl-jabber prpl-xmpp2; do
	has "$WORK/bl.dump" "BUDDY $p alice@localhost/laptop Friends bob@localhost Bobby"
	has "$WORK/bl.dump" "BUDDY $p alice@localhost/laptop Friends carol@localhost -"
	has "$WORK/bl.dump" "CHAT $p alice@localhost/laptop Friends Room One room1 conference.localhost alice 0"
	has "$WORK/bl.dump" "BUDDY $p bob@localhost/orig2 Work alice@localhost -"
	has "$WORK/bl.dump" "CHAT $p bob@localhost/orig2 Work - dev conference.localhost bob 1"
done
pass "buddies, aliases, groups and room bookmarks copied (auto-join kept)"

# state, logs
STATE="$(find "$HUSERDIR/xmpp2" -name 'alice*laptop*.state' | head -1)"
[[ -n "$STATE" ]] || fail "no state file for the clone"
grep -q '^initial=cursor-only' "$STATE" || fail "clone not marked cursor-only"
grep -q '^cursor=' "$STATE" || fail "first sync stored no cursor"
BSTATE="$(find "$HUSERDIR/xmpp2" -name 'bob*orig2*.state' | head -1)"
[[ -n "$BSTATE" ]] && grep -q '^initial=cursor-only' "$BSTATE" ||
	fail "never started clone has no cursor-only mark"
grep -q "xmpp2-migrate" "$H_ERR" || fail "no migration debug lines"
! grep "xmpp2-migrate" "$H_ERR" | grep -q -e "$XMPP_PASSWORD" ||
	fail "password in the migration debug log"
LOGDIR="$HUSERDIR/logs/jabber/alice@localhost/bob@localhost"
[[ -d "$LOGDIR" && "$(grep -l new1 "$LOGDIR"/* | wc -l)" == 1 ]] ||
	fail "new1 not logged next to the old jabber log"
[[ "$(ls "$LOGDIR" | wc -l)" -ge 2 ]] || fail "old log gone"
pass "state seeded cursor-only, history not replayed, log joins logs/jabber/<user>"

# --- 2. again: nothing new ---------------------------------------------------
run_harness carol <<SCRIPT
connect 30
action Copy XMPP accounts
no-request 2
expect-account prpl-xmpp2 alice@localhost/laptop enabled=1
expect-account prpl-jabber alice@localhost/laptop enabled=0
disconnect
SCRIPT
dump_accounts "$HUSERDIR/accounts.xml" >"$WORK/acc2.dump"
dump_blist "$HUSERDIR/blist.xml" >"$WORK/bl2.dump"
diff "$WORK/acc.dump" "$WORK/acc2.dump" >/dev/null || fail "accounts changed by the second run"
diff "$WORK/bl.dump" "$WORK/bl2.dump" >/dev/null || fail "buddy list changed by the second run"
pass "second run creates and changes nothing (idempotent)"

# --- 2b. the plugin action (Tools > Plugin Actions) --------------------------
# Pidgin shows prpl actions only under a connected account of that prpl, so
# the copy also hangs on an invisible standard plugin: no connect here, no
# xmpp2 account is online.
HUSERDIR="$WORK/ud-plugin-action"
seed "$HUSERDIR"
run_harness carol <<SCRIPT
plugin-action core-xmpp2-migrate Copy XMPP accounts
answer 0
expect-account prpl-xmpp2 bob@localhost/orig2 exists=1 enabled=0
expect-account prpl-xmpp2 alice@localhost/laptop exists=1
expect-account prpl-jabber alice@localhost/laptop enabled=0
SCRIPT
grep -q "^REQUEST Copy 2 XMPP accounts to XMPP (xmpp2)? \[Copy|Cancel\]" "$H_OUT" ||
	fail "plugin action did not ask for confirmation"
pass "the plugin action copies without a connected xmpp2 account"

# --- 3. startup offer --------------------------------------------------------
# harness UI id: no dialog
HUSERDIR="$WORK/ud-noui"
seed "$HUSERDIR"
run_harness carol <<SCRIPT
no-request 5
SCRIPT
pass "no startup dialog outside Pidgin"

if test_hooks_enabled; then
	# Not now: nothing copied, offered again next start
	HUSERDIR="$WORK/ud-offer"
	seed "$HUSERDIR"
	run_harness carol "${ANY_UI[@]}" <<-SCRIPT
	answer 1 15
	expect-account prpl-xmpp2 alice@localhost/laptop exists=0
	no-request 2
	SCRIPT
	grep -q "^OK answer: Copy 2 XMPP accounts to XMPP (xmpp2)? \[Copy|Not now|Never\] -> b1" "$H_OUT" ||
		fail "offer text or buttons wrong"
	! grep -q "name='migration_never' type='bool' value='1'" "$HUSERDIR/prefs.xml" ||
		fail "Not now was persisted"
	run_harness carol "${ANY_UI[@]}" <<-SCRIPT
	answer 1 15
	SCRIPT
	pass "Not now copies nothing and offers again next start"

	# Never: persisted, no offer on the next start
	run_harness carol "${ANY_UI[@]}" <<-SCRIPT
	answer 2 15
	expect-account prpl-xmpp2 alice@localhost/laptop exists=0
	SCRIPT
	grep -q "name='migration_never' type='bool' value='1'" "$HUSERDIR/prefs.xml" ||
		fail "Never not persisted in prefs.xml"
	run_harness carol "${ANY_UI[@]}" <<-SCRIPT
	no-request 5
	SCRIPT
	pass "Never persists across restarts"

	# the action still works after Never
	run_harness carol "${ANY_UI[@]}" <<-SCRIPT
	connect 30
	action Copy XMPP accounts
	answer 0
	expect-account prpl-xmpp2 bob@localhost/orig2 exists=1 enabled=0
	disconnect
	SCRIPT
	pass "the prpl action is independent of Never"

	# and so does the plugin action, with no connected account at all
	NEVER_PREFS="$HUSERDIR/prefs.xml"
	HUSERDIR="$WORK/ud-never-plugin"
	seed "$HUSERDIR"
	cp "$NEVER_PREFS" "$HUSERDIR/prefs.xml"
	run_harness carol "${ANY_UI[@]}" <<-SCRIPT
	plugin-action core-xmpp2-migrate Copy XMPP accounts
	answer 0
	expect-account prpl-xmpp2 bob@localhost/orig2 exists=1 enabled=0
	SCRIPT
	pass "the plugin action is independent of Never and of any connection"

	# Copy from the offer: copies, the enabled original is disabled
	HUSERDIR="$WORK/ud-offer-copy"
	seed "$HUSERDIR"
	run_harness carol "${ANY_UI[@]}" <<-SCRIPT
	answer 0 15
	expect-account prpl-xmpp2 alice@localhost/laptop enabled=1
	expect-account prpl-jabber alice@localhost/laptop enabled=0
	expect-account prpl-xmpp2 bob@localhost/orig2 enabled=0
	SCRIPT
	pass "Copy in the startup offer"
else
	echo "SKIP: dialog answers need -DXMPP2_TEST_HOOKS=ON"
fi

# --- 4. real Pidgin ------------------------------------------------------------
# Pidgin's UI id gates the offer: run it under Xvfb with a seeded profile and
# read its debug log (nobody clicks the dialog).
if command -v pidgin >/dev/null && command -v xvfb-run >/dev/null; then
	PHOME="$WORK/pidgin-home"
	PCONF="$PHOME/conf"
	mkdir -p "$PCONF/plugins"
	seed "$PCONF"
	# Pidgin takes the enabled flag of its own UI id
	sed -i "s/ui='$UI'/ui='gtk-gaim'/" "$PCONF/accounts.xml"
	cp "$PLUGIN_DIR/libxmpp2.so" "$PCONF/plugins/"
	(
		export HOME="$PHOME" XDG_CONFIG_HOME="$PHOME/.config" \
			XDG_DATA_HOME="$PHOME/.local/share" XDG_CACHE_HOME="$PHOME/.cache"
		pidgin_xvfb 20 "$PCONF" "$WORK/pidgin.log"
	) || fail "Pidgin did not exit"
	grep -q "xmpp2-migrate.*asking to copy 2 account(s) (startup offer)" "$WORK/pidgin.log" ||
		{ grep -i "migrate\|xmpp2" "$WORK/pidgin.log" | tail -20 >&2;
		  fail "Pidgin did not make the offer"; }
	pass "Pidgin (UI gtk-gaim) offers the copy at startup"
else
	echo "SKIP: pidgin or xvfb-run missing, no Pidgin offer check"
fi
