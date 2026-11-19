#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# M4: MAM for rooms (PLAN M4 items 1, 2, 5, 6, 7, 8, 10).
# Run: PRPL=prpl-xmpp2 PLUGIN_DIR=build tests/integration/test_mam_muc.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$HERE/lib.sh"
[[ "$PRPL" == "prpl-xmpp2" ]] || { echo "SKIP: needs PRPL=prpl-xmpp2"; exit 77; }
test_init mam_muc

HUSERDIR="$WORK/alice-userdir"
HLOG=txt
export HUSERDIR HLOG
ROOM=m4room@conference.localhost

# 1. carol posts r1..r5 while alice is not in the room; harness joins.
run_peer carol --do "join $ROOM carol" --do "groupsend $ROOM r1" \
	--do "sleep 1" --do "groupsend $ROOM r2" --do "sleep 1" \
	--do "groupsend $ROOM r3" --do "sleep 1" --do "groupsend $ROOM r4" \
	--do "sleep 1" --do "groupsend $ROOM r5" --do "sleep 1" \
	--do "leave $ROOM" >/dev/null
run_harness alice --debug <<SCRIPT
connect 30
join $ROOM alice
expect-next kind=recv conv=$ROOM flag=DELAYED timeout=15 texteq=r1
expect-next kind=recv conv=$ROOM flag=DELAYED texteq=r2
expect-next kind=recv conv=$ROOM flag=DELAYED texteq=r3
expect-next kind=recv conv=$ROOM flag=DELAYED texteq=r4
expect-next kind=recv conv=$ROOM flag=DELAYED texteq=r5
expect-debug cat=xmpp2-mam text=room archive $ROOM LIVE
sleep 2
count-ev kind=recv n=1 texteq=r1
count-ev kind=recv n=1 texteq=r5
no-ev timeout=1 kind=system text=Older history skipped
# 8. nick change: no new room query
cmd $ROOM nick alice2
sleep 2
count-debug cat=xmpp2-mam n=1 text=to $ROOM:
# 2. leave and rejoin, no new traffic: nothing shown again
leave $ROOM
sleep 1
join $ROOM alice
expect-debug cat=xmpp2-mam text=room archive $ROOM LIVE
sleep 2
count-debug cat=xmpp2-mam n=2 text=room archive $ROOM LIVE
count-ev kind=recv n=1 texteq=r1
count-ev kind=recv n=1 texteq=r5
disconnect
logcount $ROOM.chat r3 1
SCRIPT
grep -q "<history maxchars='0' maxstanzas='0'/>" "$H_ERR" ||
	grep -q "<history maxstanzas='0' maxchars='0'/>" "$H_ERR" ||
	fail "join presence without <history maxchars='0' maxstanzas='0'/>"
pass "join presence suppresses legacy history (item 7)"
pass "r1..r5 shown once, in order, DELAYED (item 1)"
pass "nick change sends no room query (item 8)"
pass "leave and rejoin shows nothing again (item 2)"

# 2 (restart). New session, no new traffic: nothing shown again.
run_harness alice <<SCRIPT
connect 30
join $ROOM alice
expect-debug cat=xmpp2-mam text=room archive $ROOM LIVE
sleep 2
no-ev timeout=1 kind=recv conv=$ROOM
disconnect
SCRIPT
pass "restart and rejoin shows no room history (item 2)"

# 2 (record). prpl-jabber replays legacy history on every join.
JUSERDIR="$WORK/jabber-userdir"
HUSERDIR="$JUSERDIR" PRPL=prpl-jabber run_harness alice <<SCRIPT
connect 30
join $ROOM alice
sleep 3
disconnect
SCRIPT
LEGACY="$(grep -c "^EV recv conv=$ROOM .* r[1-5]\$" "$H_OUT" || true)"
pass "prpl-jabber shows $LEGACY legacy history lines on the same rejoin"

# 5. alice/phone posts into the room while the harness is away.
# 6. carol sends a private message to alice's nick (received by alice/phone,
#    archived in alice's account archive with the muc#user hint).
PEER_RESOURCE=phone peer_bg "$WORK/phone.log" alice --stay 20 \
	--do "join $ROOM alice" --do "groupsend $ROOM own1"
PHONE_PID=$PEER_PID
wait_for_line "$WORK/phone.log" "PEER joined" 20 || fail "phone did not join"
run_peer carol --do "join $ROOM carol" --do "sleep 1" \
	--do "send $ROOM/alice pm1" --do "sleep 1" --do "leave $ROOM" >/dev/null
sleep 1
kill "$PHONE_PID" 2>/dev/null || true
wait "$PHONE_PID" 2>/dev/null || true
run_harness alice <<SCRIPT
connect 30
expect-ev kind=recv conv=$ROOM/carol ctype=im timeout=20 texteq=pm1
join $ROOM alice
expect-ev conv=$ROOM flag=SEND|DELAYED timeout=15 texteq=own1
expect-debug cat=xmpp2-mam text=room archive $ROOM LIVE
sleep 2
count-ev n=1 texteq=own1
count-ev n=1 texteq=pm1
no-ev timeout=1 kind=recv conv=$ROOM texteq=r1
disconnect
SCRIPT
pass "own message from another device shown once as ours (item 5)"
pass "MUC private message from the archive opens $ROOM/carol (item 6)"

# 10. A forged result relayed through the room (outer from room/nick) is
# dropped; live room traffic while the archive syncs is queued and shown.
peer_bg "$WORK/forge.log" carol --stay 20 --do "join $ROOM carol" \
	--do "sleep 6" \
	--do "raw <message to='$ROOM' type='groupchat'><body>carrier</body><result xmlns='urn:xmpp:mam:2' queryid='random' id='f1'><forwarded xmlns='urn:xmpp:forward:0'><message xmlns='jabber:client' type='groupchat' from='$ROOM/bob'><body>forged-1</body></message></forwarded></result></message>" \
	--do "groupsend $ROOM live1"
FORGE_PID=$PEER_PID
run_harness alice <<SCRIPT
connect 30
join $ROOM alice
expect-debug cat=xmpp2-mam timeout=20 text=forged MAM result from $ROOM/carol
expect-ev kind=recv conv=$ROOM timeout=10 texteq=live1
no-ev timeout=2 text=forged-1
no-ev timeout=1 texteq=carrier
count-ev n=1 texteq=live1
disconnect
SCRIPT
kill "$FORGE_PID" 2>/dev/null || true
pass "forged room result relayed by an occupant dropped (item 10)"
