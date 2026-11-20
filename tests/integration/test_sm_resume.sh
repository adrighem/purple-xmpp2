#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 purple-xmpp2 contributors
#
# M10 stream resumption (XEP-0198 <resume/>) as a silent reconnect:
# 1. TCP drop: resumed (no connection error, no new bind, no MAM catch-up,
#    no carbons enable, room stays joined); a message typed during the gap
#    and one bob sent during the gap are each delivered exactly once.
# 2. Deliberate sign-off: </stream> ends the session at once (bob sees
#    alice leave the room, the server does not hibernate it).
# 3. The server forgot the session: <failed/>, connection error, the next
#    login's MAM catch-up brings the gap message exactly once.
# 4. A rogue server answers the resume attempt with <resumed/> before SASL:
#    refused as a protocol error (no skipped authentication, no "resumed").
# Needs PRPL=prpl-xmpp2 and -DXMPP2_TEST_HOOKS=ON (resume delay hook).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$HERE/lib.sh"
if [[ "$PRPL" != "prpl-xmpp2" ]]; then
	echo "SKIP: test_sm_resume needs PRPL=prpl-xmpp2"
	exit 77
fi
test_hooks_enabled || { echo "SKIP: resume delay needs -DXMPP2_TEST_HOOKS=ON"; exit 77; }
test_init sm_resume

TAG="$$-$RANDOM"
ROOM="m10-$TAG@conference.localhost"
CONTAINER="${XMPP2_TEST_CONTAINER:-xmpp2-test-prosody}"
# The transport stays down this long before the first attempt.
export XMPP2_TEST_SM_RESUME_DELAY_MS=4000

# 1. Drop and resume.
peer_bg "$WORK/bob.log" bob --stay 2 --do "befriend alice@localhost" \
	--do "join $ROOM bob" \
	--do "wait gapstart-$TAG 60" \
	--do "send alice@localhost dmgap-$TAG" \
	--do "groupsend $ROOM rmgap-$TAG" \
	--do "wait typed-$TAG 60" --do "groupwait after-$TAG" --do "sleep 3" \
	--do "count-stanza <body>typed-$TAG</body> 1" \
	--do "count-stanza <body>gapstart-$TAG</body> 1" \
	--do "count-stanza <body>after-$TAG</body> 1" \
	--do "no-stanza <presence(?=[^>]*$ROOM/alice)(?=[^>]*unavailable) 1"
BOB_PID=$PEER_PID

run_harness alice <<SCRIPT
connect 30
expect-debug cat=xmpp2-sm text=enable requested (resume)
expect-debug cat=xmpp2-sm text=resume=1
join $ROOM alice
sleep 2
clear-debug
send bob@localhost gapstart-$TAG
drop
expect-debug cat=xmpp2-sm timeout=5 text=transport lost
send bob@localhost typed-$TAG
expect-debug cat=xmpp2 timeout=5 text=resuming, <message/> held
expect-debug cat=xmpp2-sm timeout=30 text=resumed previd=
expect-ev kind=recv timeout=15 text=dmgap-$TAG
expect-ev kind=recv ctype=chat timeout=15 text=rmgap-$TAG
groupsend $ROOM after-$TAG
sleep 4
count-ev kind=recv n=1 text=dmgap-$TAG
count-ev kind=recv n=1 text=rmgap-$TAG
no-ev timeout=1 kind=system text=may not have been delivered
no-debug cat=xmpp2 timeout=1 text=xmpp-bind'><resource>
no-debug cat=xmpp2-mam timeout=1 text=SYNCING
no-debug cat=xmpp2-carbons timeout=1 text=enabling carbons
no-debug cat=xmpp2 timeout=1 text=<presence to='$ROOM
dump-debug
SCRIPT
grep -q "connection error" "$H_ERR" && fail "resume raised a connection error"
grep -q "^DBG xmpp2-sm: resume attempt 1 of 2" "$H_OUT" ||
	fail "no first resume attempt"
wait "$BOB_PID" || { cat "$WORK/bob.log"; fail "bob peer failed"; }
pass "TCP drop resumed silently: gap messages once each way, room kept, no bind, no MAM catch-up"

# 2. A deliberate sign-off is not resumable: </stream> ends the session at
# once and the room sees alice leave (a hibernated session would stay).
ROOM2="m10b-$TAG@conference.localhost"
peer_bg "$WORK/bob2.log" bob --stay 1 --do "join $ROOM2 bob" \
	--do "groupwait signoff-$TAG" \
	--do "expect-stanza <presence(?=[^>]*$ROOM2/alice)(?=[^>]*unavailable) 8"
BOB_PID=$PEER_PID
run_harness alice <<SCRIPT
connect 30
expect-debug cat=xmpp2-sm text=resume=1
join $ROOM2 alice
sleep 2
groupsend $ROOM2 signoff-$TAG
sleep 2
disconnect
no-debug cat=xmpp2-sm timeout=1 text=transport lost
SCRIPT
wait "$BOB_PID" || { cat "$WORK/bob2.log"; fail "sign-off did not end the session at once"; }
pass "deliberate sign-off ends the server session (no hibernation)"

# 3. The server forgets the session while the transport is down: <failed/>,
# the usual connection error, and the next login fetches the gap from MAM.
peer_bg "$WORK/bob3.log" bob --stay 2 \
	--do "wait gap3start-$TAG 60" --do "sleep 1" \
	--do "send alice@localhost mamgap-$TAG"
BOB_PID=$PEER_PID
(
	wait_for_line "$WORK/bob3.log" "PEER recv chat .*gap3start-$TAG" 90 &&
	sleep 2 &&
	podman exec "$CONTAINER" prosodyctl shell \
		"> for _, s in pairs(prosody.full_sessions) do if s.username == 'alice' and s.resource == 'harness' then require'core.sessionmanager'.destroy_session(s, 'm10 test') end end" \
		>"$WORK/kill.log" 2>&1
) &
KILL_PID=$!
run_harness alice <<SCRIPT
connect 30
expect-debug cat=xmpp2-sm text=resume=1
sleep 2
send bob@localhost gap3start-$TAG
drop
expect-debug cat=xmpp2-sm timeout=5 text=transport lost
send bob@localhost lost-$TAG
expect-debug cat=xmpp2-sm timeout=30 text=resume failed (item-not-found)
expect-debug cat=xmpp2-sm timeout=5 text=resume given up
expect-ev kind=system timeout=10 text=may not have been delivered: lost-$TAG
connect 30
expect-ev kind=recv timeout=20 text=mamgap-$TAG
sleep 3
count-ev kind=recv n=1 text=mamgap-$TAG
SCRIPT
wait "$KILL_PID" || { cat "$WORK/kill.log"; fail "could not drop the hibernated session"; }
grep -q "connection error" "$H_ERR" || fail "failed resume did not raise a connection error"
wait "$BOB_PID" || { cat "$WORK/bob3.log"; fail "bob peer failed"; }
pass "failed resume: connection error, unacked reported, MAM fills the gap once"

# 4. A rogue server (upload_stub.py --forge-resumed) answers the resume
# attempt with a <resumed/> before any SASL: it must be refused as a protocol
# error, never acted on (the unit test of x2_sm_resume_reply_ok has the rest).
STUB_LOG="$WORK/stub-forge.log"
"$VENV/bin/python" "$HERE/upload_stub.py" --forge-resumed >"$STUB_LOG" 2>&1 &
STUB_PID=$!
wait_for_line "$STUB_LOG" "^PORT " 10 || { cat "$STUB_LOG"; fail "stub did not start"; }
STUB_PORT="$(sed -n 's/^PORT //p' "$STUB_LOG")"
export XMPP2_TEST_SM_RESUME_DELAY_MS=1000
run_harness alice --proxy "127.0.0.1:$STUB_PORT" <<SCRIPT
connect 30
expect-debug cat=xmpp2-sm text=resume=1
sleep 2
clear-debug
drop
expect-debug cat=xmpp2-sm timeout=20 text=without a <resume/> sent on this authenticated stream
expect-debug cat=xmpp2-sm timeout=5 text=resume given up
no-debug cat=xmpp2-sm timeout=1 text=resumed previd=
SCRIPT
kill "$STUB_PID" 2>/dev/null || true
wait "$STUB_PID" 2>/dev/null || true
grep -q "^STUB forged <resumed/>" "$STUB_LOG" || { cat "$STUB_LOG"; fail "the stub did not forge a <resumed/>"; }
grep -q "connection error" "$H_ERR" || fail "forged <resumed/> did not raise a connection error"
pass "a <resumed/> before SASL is a protocol error, never acted on"
