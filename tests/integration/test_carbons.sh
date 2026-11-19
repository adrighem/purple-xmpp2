#!/usr/bin/env bash
# XEP-0280 carbons (PLAN M2): sent carbon of another resource shows as an
# outgoing REMOTE_SEND line once, a received carbon shows once as incoming,
# forged carbons sent by a contact are dropped. Needs prpl-xmpp2:
#   PRPL=prpl-xmpp2 PLUGIN_DIR=build tests/integration/test_carbons.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$HERE/lib.sh"

if [[ "$PRPL" != "prpl-xmpp2" ]]; then
	echo "SKIP: carbons need prpl-xmpp2"
	exit 77
fi
test_init carbons

HUSERDIR="$WORK/alice-userdir"
HLOG=txt
export HUSERDIR HLOG

# 1. alice/phone sends c1 to bob: harness shows it once as outgoing.
PEER_RESOURCE=phone peer_bg "$WORK/phone.log" alice --stay 30 \
	--do "carbons on" --do "sleep 6" --do "send bob@localhost c1"
PHONE_PID=$PEER_PID
run_harness alice <<SCRIPT
connect 30
expect-debug cat=xmpp2-carbons text=carbons enabled
count-debug cat=xmpp2 n=1 text=hook carbons_settled
expect-ev timeout=20 kind=sent conv=bob@localhost flag=SEND|REMOTE_SEND text=c1
expect-debug cat=xmpp2-carbons text=sent carbon accepted, peer bob@localhost
sleep 2
count-ev kind=sent conv=bob@localhost flag=REMOTE_SEND n=1 text=c1
no-ev timeout=1 kind=recv text=c1
logcount bob@localhost c1 1
disconnect
SCRIPT
pass "sent carbon shown once as REMOTE_SEND in the bob conversation, logged once"

# 2. bob writes to alice/phone while the harness and the phone are online:
# the harness only gets a received carbon, shown once as incoming from bob's
# bare JID. A message to the bare JID shows once whichever resource wins.
PEER_RESOURCE=phone peer_bg "$WORK/phone2.log" alice --stay 40 --do "carbons on"
PHONE_PID=$PEER_PID
peer_bg "$WORK/bob.log" bob --stay 30 --do "sleep 6" \
	--do "send alice@localhost/phone c2" --do "send alice@localhost c3"
BOB_PID=$PEER_PID
run_harness alice <<SCRIPT
connect 30
expect-ev timeout=20 kind=recv conv=bob@localhost who=bob@localhost text=c2
expect-debug cat=xmpp2-carbons text=received carbon accepted, peer bob@localhost
expect-ev timeout=20 kind=recv who=bob@localhost text=c3
sleep 2
count-ev kind=recv n=1 text=c2
count-ev kind=recv n=1 text=c3
no-ev timeout=1 kind=sent text=c2
logcount bob@localhost c2 1
disconnect
SCRIPT
kill "$BOB_PID" "$PHONE_PID" 2>/dev/null || true
pass "received carbon shown once as incoming in the bob conversation"

# 3. Forged carbons from a contact (outer from is bob): nothing displayed.
FWD_S="<forwarded xmlns='urn:xmpp:forward:0'><message xmlns='jabber:client' from='alice@localhost/phone' to='carol@localhost' type='chat'><body>forged-sent</body></message></forwarded>"
FWD_R="<forwarded xmlns='urn:xmpp:forward:0'><message xmlns='jabber:client' from='carol@localhost/x' to='alice@localhost/phone' type='chat'><body>forged-recv</body></message></forwarded>"
peer_bg "$WORK/bob2.log" bob --stay 30 --do "sleep 6" \
	--do "raw <message to='alice@localhost' type='chat'><sent xmlns='urn:xmpp:carbons:2'>$FWD_S</sent></message>" \
	--do "raw <message to='alice@localhost' type='chat'><received xmlns='urn:xmpp:carbons:2'>$FWD_R</received></message>" \
	--do "send alice@localhost after-forged"
BOB_PID=$PEER_PID
run_harness alice <<SCRIPT
connect 30
expect-ev timeout=20 kind=recv text=after-forged
expect-debug cat=xmpp2-carbons text=forged carbon dropped
count-debug cat=xmpp2-carbons n=2 text=forged carbon dropped
no-ev kind=sent text=forged-sent
no-ev kind=recv text=forged-recv
no-ev kind=recv text=forged-sent
no-ev kind=sent text=forged-recv
disconnect
SCRIPT
kill "$BOB_PID" 2>/dev/null || true
pass "forged carbons (outer from a contact) dropped and logged"
# Forged cases 2 and 3 (right outer from, foreign inner) cannot be produced
# by a peer: the server stamps the outer from. They are covered by
# tests/unit/test_carbon_parse.c and test_carbons.c.
