#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 purple-xmpp2 contributors
# M5: delivery receipts (XEP-0184). Harness (alice) against peers bob (roster
# contact) and carol (stranger). Run with:
#   PRPL=prpl-xmpp2 PLUGIN_DIR=build tests/integration/test_receipts.sh
# Receipts are an xmpp2 feature; with prpl-jabber the script is skipped.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$HERE/lib.sh"

if [[ "$PRPL" != "prpl-xmpp2" ]]; then
	echo "SKIP: receipts are an xmpp2 feature (PRPL=$PRPL)"
	exit 77
fi
test_init receipts

ROOM=test@conference.localhost
RCPT_REQ='<request[^>]*urn:xmpp:receipts'
MARKABLE='<markable[^>]*urn:xmpp:chat-markers:0'
RECEIVED='<received[^>]*urn:xmpp:receipts'
FEAT=(--feature urn:xmpp:receipts --feature urn:xmpp:chat-markers:0)
HARNESS_OPTS=(--userdir "$WORK/alice-userdir" --log txt)

# 0. bob and alice subscribe to each other. The harness cannot approve a
# subscription request, so a second alice resource (slixmpp auto-approves) does.
PEER_RESOURCE=phone peer_bg "$WORK/phone.log" alice --stay 6
PHONE_PID=$PEER_PID
run_peer bob --do "befriend alice@localhost" --do "sleep 4" >/dev/null
wait "$PHONE_PID" || true

# 1. Outgoing: 1:1 gets request and markable when bob's caps allow it,
# groupchat gets neither. bob answers, so the xmpp2-receipt signal and the
# notice line fire.
peer_bg "$WORK/bob.log" bob --stay 1 "${FEAT[@]}" --do "receipts on" \
	--do "join $ROOM bob" --do "wait out-1" \
	--do "expect-stanza $RCPT_REQ" --do "expect-stanza $MARKABLE" \
	--do "groupwait room-1" \
	--do "no-stanza groupchat.*<(request|markable)"
BOB_PID=$PEER_PID
run_harness alice "${HARNESS_OPTS[@]}" <<SCRIPT
connect 30
pref b:xmpp2_receipt_notices=1
sleep 2
send bob@localhost out-1
expect-debug cat=xmpp2-receipts text=signal xmpp2-receipt
expect-ev kind=system flag=NO_LOG text=Message delivered.
join $ROOM alice
groupsend $ROOM room-1
sleep 4
logcount bob@localhost Message delivered. 0
disconnect
SCRIPT
wait "$BOB_PID" || { cat "$WORK/bob.log"; fail "request/markable missing, or present in groupchat"; }
pass "1:1 messages carry request and markable, groupchat none; signal and notice"

# 1b. A contact whose caps lack the namespaces gets no request.
peer_bg "$WORK/bob1b.log" bob --stay 1 --do "wait plain-1" \
	--do "no-stanza <(request|markable) 3"
BOB_PID=$PEER_PID
run_harness alice "${HARNESS_OPTS[@]}" <<SCRIPT
connect 30
sleep 3
send bob@localhost plain-1
sleep 4
disconnect
SCRIPT
wait "$BOB_PID" || { cat "$WORK/bob1b.log"; fail "request sent to a contact without receipts caps"; }
pass "no request when the contact's caps lack receipts and markers"

# 2. Incoming requests: bob (subscribed) gets a receipt for a live message,
# carol (not in the roster) gets none.
peer_bg "$WORK/bob2.log" bob --stay 1 --do "wait ping-b" \
	--do "sendr alice@localhost live-b" --do "expect-stanza $RECEIVED"
BOB_PID=$PEER_PID
peer_bg "$WORK/carol.log" carol --stay 1 --do "wait ping-c" \
	--do "sendr alice@localhost live-c" --do "no-stanza $RECEIVED 5"
CAROL_PID=$PEER_PID
run_harness alice "${HARNESS_OPTS[@]}" <<SCRIPT
connect 30
send bob@localhost ping-b
send carol@localhost ping-c
wait live-b 20
wait live-c 20
sleep 5
count-debug cat=xmpp2-receipts n=1 text=sent receipt for
expect-debug cat=xmpp2-receipts text=no presence subscription
disconnect
SCRIPT
wait "$BOB_PID" || { cat "$WORK/bob2.log"; fail "bob got no receipt"; }
wait "$CAROL_PID" || { cat "$WORK/carol.log"; fail "stranger got a receipt"; }
pass "receipt for a roster contact, none for a stranger"

# 2b. A live request that arrives during the MAM catch-up is answered at
# once, before mam queues the message (the archive page may show it and drop
# the live copy). XMPP2_TEST_MAM_STALL keeps the archive SYNCING.
if test_hooks_enabled; then
	peer_bg "$WORK/bob2b.log" bob --stay 1 --do "sleep 4" \
		--do "sendr alice@localhost sync-b" --do "expect-stanza $RECEIVED"
	BOB_PID=$PEER_PID
	XMPP2_TEST_MAM_STALL=1 run_harness alice "${HARNESS_OPTS[@]}" <<SCRIPT
connect 30
expect-debug cat=xmpp2-mam timeout=15 text=queued during catch-up
expect-debug cat=xmpp2-receipts timeout=2 text=sent receipt for
no-debug timeout=1 cat=xmpp2-mam text=account archive LIVE
disconnect
SCRIPT
	wait "$BOB_PID" || { cat "$WORK/bob2b.log"; fail "no receipt during catch-up"; }
	pass "request answered while the message waits for the catch-up"
else
	echo "SKIP 2b: needs -DXMPP2_TEST_HOOKS=ON"
fi

# 3. A message that waited in the server's offline store (delayed) is still
# a live stanza: its sender (bob stays online) gets the receipt on login.
peer_bg "$WORK/bob3.log" bob --stay 1 --do "sendr alice@localhost offline-b" \
	--do "expect-stanza $RECEIVED 25"
BOB_PID=$PEER_PID
run_harness alice "${HARNESS_OPTS[@]}" <<SCRIPT
connect 30
expect-ev timeout=15 kind=recv text=offline-b
expect-debug cat=xmpp2-receipts timeout=10 text=sent receipt for
sleep 1
disconnect
SCRIPT
wait "$BOB_PID" || { cat "$WORK/bob3.log"; fail "no receipt for an offline (delayed) message"; }
pass "receipt for offline (delayed) messages on login"

# 4. A carbon copy (bob wrote to our phone, the harness only gets a received
# carbon) gets no receipt from the harness, although bob is subscribed.
PEER_RESOURCE=phone peer_bg "$WORK/phone4.log" alice --stay 25 --do "carbons on"
PHONE_PID=$PEER_PID
peer_bg "$WORK/bob4.log" bob --stay 1 --do "sleep 6" \
	--do "sendr alice@localhost/phone carbon-b" --do "no-stanza $RECEIVED 8"
BOB_PID=$PEER_PID
run_harness alice "${HARNESS_OPTS[@]}" <<SCRIPT
connect 30
expect-ev timeout=20 kind=recv text=carbon-b
expect-debug cat=xmpp2-carbons text=received carbon accepted
sleep 4
no-debug timeout=1 cat=xmpp2-receipts text=sent receipt for
disconnect
SCRIPT
wait "$BOB_PID" || { cat "$WORK/bob4.log"; fail "receipt for a carbon copy"; }
wait "$PHONE_PID" || true
pass "no receipt for carbon-copied messages"

# 5. A private message to a room occupant: xmpp2-sent-message names the
# room/nick conversation (not the room), and no request goes out.
peer_bg "$WORK/bob5.log" bob --stay 1 "${FEAT[@]}" --do "join $ROOM bob" \
	--do "wait pm-1" --do "no-stanza <(request|markable)"
BOB_PID=$PEER_PID
run_harness alice "${HARNESS_OPTS[@]}" <<SCRIPT
connect 30
join $ROOM alice
sleep 2
send $ROOM/bob pm-1
expect-debug cat=xmpp2-receipts text=signal xmpp2-sent-message account=alice@localhost/harness peer=$ROOM/bob id=
sleep 2
disconnect
SCRIPT
wait "$BOB_PID" || { cat "$WORK/bob5.log"; fail "MUC private message: a request was sent"; }
pass "MUC private message signals room/nick as the peer and asks no receipt"
