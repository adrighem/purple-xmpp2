#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# M6: message correction, XEP-0308 (PLAN M6): incoming marker, rejected
# foreign corrections (1:1 and room), /correct outgoing, MAM-delivered
# corrections, caps. Run:
#   PRPL=prpl-xmpp2 PLUGIN_DIR=build tests/integration/test_correct.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$HERE/lib.sh"
[[ "$PRPL" == "prpl-xmpp2" ]] || { echo "SKIP: needs PRPL=prpl-xmpp2"; exit 77; }
test_init correct

HUSERDIR="$WORK/alice-userdir"
HLOG=txt
export HUSERDIR HLOG

NS="urn:xmpp:message-correct:0"
ROOM="m6room@conference.localhost"

# 1+2. bob corrects his own message (marked, logged); bob "corrects" carol's
# message and an unknown one (both shown as plain messages).
peer_bg "$WORK/carol.log" carol --stay 40 --do "sleep 6" \
	--do "send alice@localhost carol-orig"
peer_bg "$WORK/bob.log" bob --stay 40 --do "sleep 8" \
	--do "raw <message to='alice@localhost' type='chat' id='t1'><body>teh</body></message>" \
	--do "raw <message to='alice@localhost' type='chat' id='t2'><body>the</body><replace xmlns='$NS' id='t1'/></message>" \
	--do "raw <message to='alice@localhost' type='chat' id='t3'><body>forged</body><replace xmlns='$NS' id='carol-orig'/></message>" \
	--do "raw <message to='alice@localhost' type='chat' id='t4'><body>lost</body><replace xmlns='$NS' id='nope'/></message>"
run_harness alice <<SCRIPT
connect 30
expect-ev timeout=25 kind=recv conv=bob@localhost texteq=teh
expect-next kind=recv conv=bob@localhost texteq=corrected: the
expect-ev timeout=10 kind=recv conv=bob@localhost texteq=forged
expect-ev timeout=10 kind=recv conv=bob@localhost texteq=lost
expect-debug cat=xmpp2-correct text=correction of t1 accepted
expect-debug cat=xmpp2-correct text=original not seen
sleep 1
count-ev kind=recv conv=bob@localhost n=1 text=corrected:
no-ev kind=recv text=corrected: forged
no-ev kind=recv text=corrected: lost
logcount bob@localhost corrected: the 1
disconnect
SCRIPT
pass "own correction marked and logged, foreign and unknown ids shown plain"

# 3. Room: another occupant cannot correct bob's message, bob can.
peer_bg "$WORK/rbob.log" bob --stay 40 --do "join $ROOM bobnick" \
	--do "sleep 10" \
	--do "raw <message to='$ROOM' type='groupchat' id='g1'><body>room-teh</body></message>" \
	--do "sleep 1" \
	--do "raw <message to='$ROOM' type='groupchat' id='g3'><body>room-the</body><replace xmlns='$NS' id='g1'/></message>"
peer_bg "$WORK/rcarol.log" carol --stay 40 --do "join $ROOM carolnick" \
	--do "sleep 12" \
	--do "raw <message to='$ROOM' type='groupchat' id='g2'><body>room-evil</body><replace xmlns='$NS' id='g1'/></message>"
run_harness alice <<SCRIPT
connect 30
join $ROOM alicenick
expect-ev timeout=25 kind=recv ctype=chat text=room-teh
expect-ev timeout=10 kind=recv ctype=chat texteq=room-evil
expect-ev timeout=10 kind=recv ctype=chat texteq=corrected: room-the
expect-debug cat=xmpp2-correct text=other author
no-ev kind=recv ctype=chat text=corrected: room-evil
leave $ROOM
disconnect
SCRIPT
pass "room correction accepted from the author only"

# 4. /correct: peer receives a replace with the id of the previous message.
peer_bg "$WORK/bob2.log" bob --stay 40 \
	--do "bind M1 (?s)<message[^>]* id=['\"]([^'\"]+)['\"][^>]*>(?:(?!</message>).)*<body>first1</body>" \
	--do "expect-stanza (?s)<body>second</body>.*<replace[^>]* id=['\"]\${M1}['\"]" \
	--do "expect-stanza (?s)<body>third</body>.*<replace[^>]* id=['\"]\${M1}['\"]" \
	--do "disco alice@localhost/harness" \
	--do "sleep 2"
BOB_PID=$PEER_PID
run_harness alice <<SCRIPT
connect 30
send bob@localhost first1
sleep 1
correct bob@localhost second
sleep 1
correct bob@localhost third
sleep 3
count-ev kind=sent conv=bob@localhost n=3
disconnect
SCRIPT
wait "$BOB_PID" || { cat "$WORK/bob2.log" >&2; fail "peer did not see the replace of the first id"; }
grep -q "PEER-FEATURE $NS" "$WORK/bob2.log" || fail "caps lack $NS"
pass "/correct sends <replace> with the first message id, caps advertise $NS"

# 5. Corrections delivered while offline (offline store or MAM) show marked.
peer bob --do "raw <message to='alice@localhost' type='chat' id='o1'><body>off-teh</body></message>" \
	--do "raw <message to='alice@localhost' type='chat' id='o2'><body>off-the</body><replace xmlns='$NS' id='o1'/></message>" \
	>"$WORK/offline.log" 2>&1
run_harness alice <<SCRIPT
connect 30
expect-ev timeout=20 kind=recv conv=bob@localhost texteq=off-teh
expect-ev timeout=20 kind=recv conv=bob@localhost texteq=corrected: off-the
sleep 3
count-ev kind=recv n=1 text=corrected: off-the
disconnect
SCRIPT
pass "offline/MAM-delivered correction marked, once"
