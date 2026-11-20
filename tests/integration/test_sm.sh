#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 purple-xmpp2 contributors
#
# M9 stream management (XEP-0198) with the account option sm_resume off
# (the v0.1.0 path; resumption is test_sm_resume.sh): plain enable,
# batched acks, XML console stanzas counted, no resend on a fresh session
# after a TCP drop, unacked messages reported on a drop and on sign-off.
# Asserts on xmpp2-sm debug lines, so it needs PRPL=prpl-xmpp2.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$HERE/lib.sh"
if [[ "$PRPL" != "prpl-xmpp2" ]]; then
	echo "SKIP: test_sm needs PRPL=prpl-xmpp2"
	exit 77
fi
test_init sm

TAG="$$-$RANDOM"

# 1. The server offers SM; with sm_resume off we enable it without resume
# and parse <enabled/>.
run_peer bob --do streamfeatures | grep -q "PEER-STREAM stream_management" ||
	fail "server does not offer stream management"

peer_bg "$WORK/bob.log" bob --stay 2 --do "befriend alice@localhost" \
	--do "wait burst-$TAG-20 30" --do "wait drop-$TAG 30" --do "sleep 12" \
	--do "count-stanza <body>drop-$TAG</body> 1"
BOB_PID=$PEER_PID

{
	cat <<SCRIPT
connect 30
expect-debug cat=xmpp2-sm text=enable requested (no resume)
expect-debug cat=xmpp2-sm text=resume=0
sleep 3
clear-debug
SCRIPT
	# 2. A burst of 20 messages is acked in batches, not 20 <r/>.
	for i in $(seq 1 20); do
		echo "send bob@localhost burst-$TAG-$i"
	done
	cat <<SCRIPT
expect-debug cat=xmpp2-sm timeout=10 text=ack h=
sleep 3
dump-debug
clear-debug
SCRIPT
	# 2b. Raw XML (the XML console) is counted: two stanzas in one write,
	# then an ack that would be "too high" if they were not.  An incomplete
	# element is refused while SM is on.
	cat <<SCRIPT
raw <message to='bob@localhost' type='chat' id='raw-$TAG'><body>raw-$TAG</body></message><iq type='get' id='rawping-$TAG' to='localhost'><ping xmlns='urn:xmpp:ping'/></iq>
expect-debug cat=xmpp2-sm timeout=10 text=, 0 unacked
no-debug cat=xmpp2-sm timeout=1 text=but only
raw-refused <message to='bob@localhost'>
expect-debug cat=xmpp2-sm text=raw XML refused
clear-debug
SCRIPT
	# 3. Drop TCP with one message unacked (the idle <r/> is 2 s away).
	cat <<SCRIPT
send bob@localhost drop-$TAG
drop
expect-ev kind=system timeout=10 text=may not have been delivered: drop-$TAG
expect-debug cat=xmpp2-sm text=unacked stanzas (1 messages reported
connect 30
expect-debug cat=xmpp2-sm text=0 stale stanzas dropped, nothing resent
expect-debug cat=xmpp2-sm text=enabled id=
sleep 4
no-debug cat=xmpp2-sm timeout=1 text=failed
send bob@localhost bye-$TAG
disconnect
expect-ev kind=system timeout=5 text=it may not have been delivered: bye-$TAG
expect-debug cat=xmpp2-sm text=signed off with 1 unacked stanzas (1 messages reported
SCRIPT
} >"$WORK/alice.in"
run_harness alice --pref b:sm_resume=0 <"$WORK/alice.in"

requests=$(grep -c "^DBG xmpp2-sm: request ack" "$H_OUT" || true)
echo "ack requests for 20 messages: $requests"
[[ "$requests" -ge 1 && "$requests" -le 5 ]] ||
	fail "want 1..5 <r/> for a 20 message burst, got $requests"
grep -q "^DBG xmpp2-sm: ack h=.*, 0 unacked" "$H_OUT" ||
	fail "burst was never fully acked"

wait "$BOB_PID" || { cat "$WORK/bob.log"; fail "bob peer failed"; }
grep -q "PEER recv chat .*: burst-$TAG-20" "$WORK/bob.log" || fail "bob missed the burst"
grep -q "PEER count <body>drop-$TAG</body> = 1" "$WORK/bob.log" || fail "bob did not get drop-$TAG exactly once"
pass "SM plain enable, batched acks ($requests <r/>), raw XML counted, drop and sign-off reported, no resend"

# 4. Signed off with an unacked message to somebody we have no conversation
# with (sent as raw XML): no conversation is opened for the notice, it only
# goes to the debug log.
run_harness alice <<SCRIPT
connect 30
raw <message to='carol@localhost' type='chat' id='noconv-$TAG'><body>noconv-$TAG</body></message>
disconnect
expect-debug cat=xmpp2-sm text=no open conversation with carol@localhost
no-ev timeout=1 kind=system text=noconv-$TAG
SCRIPT
pass "sign-off notice for an unacked message opens no conversation"
