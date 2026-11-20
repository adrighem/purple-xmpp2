#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 purple-xmpp2 contributors
# pidgin-xmpp2-ticks end to end: a real Pidgin under Xvfb signs alice in with
# prpl-xmpp2 and the ticks plugin; its test hook sends one line to bob through
# the normal conversation path. bob (slixmpp) answers with a receipt and a
# displayed marker. The view must show the line with one tick, then two; the
# hook then clears the scrollback and closes the conversation. Run with:
#   PRPL=prpl-xmpp2 PLUGIN_DIR=build TICKS_SO=build/pidgin-plugin/pidgin-xmpp2-ticks.so \
#     XMPP2_TEST_HOOKS=1 tests/integration/test_ticks.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$HERE/lib.sh"

if [[ "$PRPL" != "prpl-xmpp2" ]]; then
	echo "SKIP: ticks are an xmpp2 feature (PRPL=$PRPL)"
	exit 77
fi
if ! test_hooks_enabled; then
	echo "SKIP: needs -DXMPP2_TEST_HOOKS=ON"
	exit 77
fi
if [[ ! -f "${TICKS_SO:-}" || ! -f "$PLUGIN_DIR/libxmpp2.so" ]]; then
	echo "SKIP: TICKS_SO and PLUGIN_DIR/libxmpp2.so must be built"
	exit 77
fi
for tool in pidgin xvfb-run; do
	command -v "$tool" >/dev/null || { echo "SKIP: $tool missing"; exit 77; }
done
test_init ticks

CHECK=$'✓'
FEAT=(--feature urn:xmpp:receipts --feature urn:xmpp:chat-markers:0)

# 0. bob and alice subscribe to each other. A second alice resource
# (slixmpp auto-approves) answers the request.
PEER_RESOURCE=phone peer_bg "$WORK/phone.log" alice --stay 6
PHONE_PID=$PEER_PID
run_peer bob --do "befriend alice@localhost" --do "sleep 4" >/dev/null
wait "$PHONE_PID" || true

# 1. Pidgin profile in $WORK only: both plugins, the ticks plugin saved as
# loaded, one enabled account, the server certificate trusted.
CONF="$WORK/pidgin"
mkdir -p "$CONF/plugins" "$CONF/certificates/x509/tls_peers" "$WORK/home"
cp "$PLUGIN_DIR/libxmpp2.so" "$TICKS_SO" "$CONF/plugins/"
cp "$SCRATCH/server.crt" "$CONF/certificates/x509/tls_peers/localhost"
cp "$SCRATCH/server.crt" "$CONF/certificates/x509/tls_peers/127.0.0.1"
cat >"$CONF/prefs.xml" <<PREFS
<?xml version='1.0' encoding='UTF-8' ?>
<pref version='1' name='/'>
<pref name='pidgin'><pref name='plugins'>
<pref name='loaded' type='pathlist'>
<item value='$CONF/plugins/$(basename "$TICKS_SO")'/>
</pref></pref></pref>
</pref>
PREFS
cat >"$CONF/accounts.xml" <<ACCOUNTS
<?xml version='1.0' encoding='UTF-8' ?>
<account version='1.0'>
<account>
<protocol>prpl-xmpp2</protocol>
<name>alice@localhost/pidgin</name>
<password>$XMPP_PASSWORD</password>
<settings>
<setting name='connect_server' type='string'>127.0.0.1</setting>
<setting name='port' type='int'>$XMPP_C2S_PORT</setting>
<setting name='connection_security' type='string'>opportunistic_tls</setting>
<setting name='auth_plain_in_clear' type='bool'>1</setting>
</settings>
<settings ui='gtk-gaim'>
<setting name='auto-login' type='bool'>1</setting>
</settings>
</account>
</account>
ACCOUNTS

# 2. bob answers every receipt request and markable message at once.
peer_bg "$WORK/bob.log" bob --stay 2 "${FEAT[@]}" --do "receipts on" \
	--do "markers on" --do "wait ticks-test-1 40"
BOB_PID=$PEER_PID

LOG="$WORK/pidgin.log"
(
	export HOME="$WORK/home" XDG_CONFIG_HOME="$WORK/home/.config" \
		XDG_DATA_HOME="$WORK/home/.local/share" \
		XDG_CACHE_HOME="$WORK/home/.cache" \
		XMPP2_TICKS_TEST_PEER=bob@localhost
	pidgin_xvfb 25 "$CONF" "$LOG"
) || fail "Pidgin did not exit"

show() {
	grep -E "xmpp2-ticks|xmpp2-receipts|xmpp2-markers|g_log|connection|account" \
		"$LOG" | tail -40 >&2
}
wait "$BOB_PID" || { cat "$WORK/bob.log"; show; fail "bob never got the line"; }

need() { grep -q -E -- "$1" "$LOG" || { show; fail "$2"; }; }

need "xmpp2-ticks: connected to prpl-xmpp2 signals" "plugin not connected"
need "xmpp2-ticks: test: sent to bob@localhost" "test hook did not send"
need "signal xmpp2-sent-message .*peer=bob@localhost" "prpl did not emit xmpp2-sent-message"
need "xmpp2-ticks: line for .* recorded" "sent line not tied to its id"
need "xmpp2-ticks: test: line .* reads \[.*ticks-test-1 $CHECK\]" "no single tick after the receipt"
need "xmpp2-ticks: test: line .* reads \[.*ticks-test-1 $CHECK$CHECK\]" "no double tick after the displayed marker"
need "xmpp2-ticks: test: after clear 0 lines tracked" "cleared view still tracks lines"
need "xmpp2-ticks: test: conversation closed" "conversation not closed"
if grep -E "signals: (Something tried|Signal data for)|g_log: .*(tick|xmpp2|imhtml|gtk_text|GtkTextBuffer)" "$LOG"; then
	show
	fail "GLib-CRITICAL or signal error"
fi
grep "xmpp2-ticks: " "$LOG"
pass "one tick on delivery, two on display, clear and close without criticals"
