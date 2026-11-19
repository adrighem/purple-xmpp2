#!/usr/bin/env bash
# M5: chat markers (XEP-0333) and the disco features of M5. Harness (alice)
# against peers bob and carol. Run with:
#   PRPL=prpl-xmpp2 PLUGIN_DIR=build tests/integration/test_markers.sh
# Markers are an xmpp2 feature; with prpl-jabber the script is skipped.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$HERE/lib.sh"

if [[ "$PRPL" != "prpl-xmpp2" ]]; then
	echo "SKIP: chat markers are an xmpp2 feature (PRPL=$PRPL)"
	exit 77
fi
test_init markers

MARK="xmlns='urn:xmpp:chat-markers:0'"
FEAT=(--feature urn:xmpp:receipts --feature urn:xmpp:chat-markers:0)

# bob sends a markable message with a known id
markable() { # ID TEXT
	echo "raw <message to='alice@localhost' type='chat' id='$1'><body>$2</body><markable $MARK/></message>"
}

# 1. Displayed marker: one for the newest markable message when the
# conversation is focused, none for a second focus, a new one after a new
# message. bob also discos the harness here, for step 3.
peer_bg "$WORK/bob.log" bob --stay 1 "${FEAT[@]}" --do "wait go" \
	--do "disco alice@localhost/harness" \
	--do "$(markable mk-1 m1)" --do "$(markable mk-2 m2)" \
	--do "expect-stanza <displayed[^>]*id=.mk-2." \
	--do "sleep 3" \
	--do "count-stanza <displayed 1" \
	--do "no-stanza <displayed[^>]*id=.mk-1. 1" \
	--do "$(markable mk-3 m3)" \
	--do "expect-stanza <displayed[^>]*id=.mk-3." \
	--do "sleep 2" \
	--do "count-stanza <displayed 2"
BOB_PID=$PEER_PID
run_harness alice <<SCRIPT
connect 30
send bob@localhost go
wait m2 20
focus bob@localhost
focus bob@localhost
wait m3 30
focus bob@localhost
sleep 4
count-debug cat=xmpp2-markers n=2 text=sent displayed
expect-debug cat=xmpp2-markers text=already sent
disconnect
SCRIPT
wait "$BOB_PID" || { cat "$WORK/bob.log"; fail "displayed marker behaviour"; }
pass "one displayed for the newest markable message, none on a repeated focus"

# 2. Incoming markers: bob displays our message (markers on); a stranger's
# marker for an id we never sent is ignored. Pure markers are not shown.
peer_bg "$WORK/bob2.log" bob --stay 1 "${FEAT[@]}" --do "markers on" \
	--do "wait mine-1" --do "sleep 3"
BOB_PID=$PEER_PID
peer_bg "$WORK/carol.log" carol --stay 1 --do "wait go-c" \
	--do "raw <message to='alice@localhost' type='chat'><displayed $MARK id='forged-1'/></message>" \
	--do "sleep 3"
CAROL_PID=$PEER_PID
run_harness alice --userdir "$WORK/alice-userdir" --log txt <<SCRIPT
connect 30
pref b:xmpp2_receipt_notices=1
sleep 2
send bob@localhost mine-1
expect-debug cat=xmpp2-markers text=signal xmpp2-marker
expect-debug cat=xmpp2-markers text=marker=displayed
expect-ev kind=system flag=NO_LOG text=Message read.
send carol@localhost go-c
expect-debug timeout=10 cat=xmpp2-markers text=ignoring displayed marker
count-ev kind=system n=1 text=Message read.
no-ev timeout=1 kind=recv text=forged
logcount bob@localhost Message read. 0
disconnect
SCRIPT
wait "$BOB_PID" || { cat "$WORK/bob2.log"; fail "bob peer failed"; }
wait "$CAROL_PID" || { cat "$WORK/carol.log"; fail "carol peer failed"; }
pass "xmpp2-marker signal and notice for ours, forged marker ignored"

# 3. Caps: both namespaces are advertised (disco of the harness, run by bob
# in step 1).
grep -q 'PEER-FEATURE urn:xmpp:receipts$' "$WORK/bob.log" ||
	{ cat "$WORK/bob.log"; fail "receipts not advertised"; }
grep -q 'PEER-FEATURE urn:xmpp:chat-markers:0$' "$WORK/bob.log" ||
	fail "chat markers not advertised"
pass "disco advertises urn:xmpp:receipts and urn:xmpp:chat-markers:0"
