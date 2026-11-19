#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# M3: MAM catch-up of the account archive (PLAN M3 items 1-7, 11a, 13).
# Run: PRPL=prpl-xmpp2 PLUGIN_DIR=build tests/integration/test_mam.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$HERE/lib.sh"
[[ "$PRPL" == "prpl-xmpp2" ]] || { echo "SKIP: needs PRPL=prpl-xmpp2"; exit 77; }
test_init mam

HUSERDIR="$WORK/alice-userdir"
HLOG=txt
export HUSERDIR HLOG

# stamp_peer ARGS...: run a foreground peer, prefix each output line with
# the epoch second it was printed.
stamp_peer() {
	peer "$@" 2>&1 | while IFS= read -r line; do
		echo "$(date +%s) $line"
	done
	[[ "${PIPESTATUS[0]}" == 0 ]] || fail "peer ($1) failed"
}

# ev_mtime TEXT: mtime of the first EV line in $H_OUT ending in TEXT
ev_mtime() {
	sed -n "s/^EV .* mtime=\([0-9]*\) .* $1\$/\1/p" "$H_OUT" | head -1
}

# 1+2+7. Fresh account, harness offline: bob sends m1..m3 with 2 s gaps.
# Prosody also stores them offline (mod_offline): each must show once.
stamp_peer bob --do "send alice@localhost m1" --do "sleep 2" \
	--do "send alice@localhost m2" --do "sleep 2" \
	--do "send alice@localhost m3" >"$WORK/sent.log"
run_harness alice --debug <<SCRIPT
connect 30
expect-next kind=recv conv=bob@localhost flag=DELAYED timeout=20 texteq=m1
expect-next kind=recv conv=bob@localhost flag=DELAYED texteq=m2
expect-next kind=recv conv=bob@localhost flag=DELAYED texteq=m3
expect-debug cat=xmpp2-mam text=account archive LIVE
expect-debug cat=xmpp2-mam text=queued duplicate
sleep 3
count-ev kind=recv n=1 texteq=m1
count-ev kind=recv n=1 texteq=m2
count-ev kind=recv n=1 texteq=m3
disconnect
logcount bob@localhost m1 1
logcount bob@localhost m2 1
logcount bob@localhost m3 1
SCRIPT
for m in m1 m2 m3; do
	sent="$(sed -n "s/^\([0-9]*\) PEER sent .*: $m\$/\1/p" "$WORK/sent.log")"
	got="$(ev_mtime "$m")"
	[[ -n "$sent" && -n "$got" ]] || fail "no send time or mtime for $m"
	(( got - sent <= 2 && sent - got <= 2 )) ||
		fail "$m mtime $got not within 2 s of send time $sent"
done
pass "offline messages: in order, DELAYED, original times, once (items 2, 7)"

# 13. Ordering: MAM query only after session_ready and, with a carbons
# module, after its enable result (hook carbons_settled).
first_query="$(grep -n 'MAM query' "$H_ERR" | head -1 | cut -d: -f1)"
ready="$(grep -n 'hook session_ready' "$H_ERR" | head -1 | cut -d: -f1)"
[[ -n "$first_query" && -n "$ready" ]] || fail "no MAM query or session_ready"
(( ready < first_query )) || fail "MAM query before session_ready"
settled="$(grep -n 'hook carbons_settled' "$H_ERR" | head -1 | cut -d: -f1 || true)"
if [[ -n "$settled" ]]; then
	(( settled < first_query )) || fail "MAM query before carbons settled"
	pass "carbons enable result before the first MAM query (item 13)"
else
	pass "no carbons module: MAM query after session_ready (item 13)"
fi

STATE="$(find "$HUSERDIR/xmpp2" -name '*.state' | head -1)"
[[ -n "$STATE" ]] || fail "no state file"
[[ "$(stat -c %a "$HUSERDIR/xmpp2")" == 700 ]] || fail "state dir not 0700"
grep -q '^cursor=' "$STATE" || fail "state file has no cursor"
[[ "$(basename "$STATE")" == alice*localhost*harness.state ]] ||
	fail "state file not named after the full username: $STATE"
pass "state file $(basename "$STATE"), dir mode 0700 (item 1)"

# 3. Restart with no new traffic: nothing shown again.
run_harness alice <<SCRIPT
connect 30
expect-debug cat=xmpp2-mam text=account archive LIVE
no-ev timeout=3 kind=recv
no-ev timeout=1 kind=sent
disconnect
SCRIPT
pass "restart without traffic shows no history (item 3)"

# 4. Harness offline, alice/phone sends p1 to carol: outgoing line on connect.
PEER_RESOURCE=phone run_peer alice --do "send carol@localhost p1" \
	--do "sleep 1" >/dev/null
# 6. Harness sends h1 to bob (offline), quits.
# 5. alice/phone sends p2 while the harness is online.
PEER_RESOURCE=phone peer_bg "$WORK/phone.log" alice --stay 30 --do "sleep 4" \
	--do "send carol@localhost p2"
PHONE_PID=$PEER_PID
run_harness alice <<SCRIPT
connect 30
expect-ev kind=sent conv=carol@localhost flag=REMOTE_SEND timeout=20 texteq=p1
count-ev kind=sent n=1 texteq=p1
send bob@localhost h1
expect-ev kind=sent conv=bob@localhost texteq=h1
sleep 6
disconnect
SCRIPT
kill "$PHONE_PID" 2>/dev/null || true
P2_FIRST="$(grep -c '^EV sent .* p2$' "$H_OUT" || true)"
pass "message from our other device while offline shown as sent (item 4)"

run_harness alice <<SCRIPT
connect 30
expect-debug cat=xmpp2-mam text=account archive LIVE
sleep 2
no-ev timeout=1 kind=sent texteq=h1
no-ev timeout=1 kind=sent texteq=p1
disconnect
SCRIPT
P2_SECOND="$(grep -c '^EV sent .* p2$' "$H_OUT" || true)"
(( P2_FIRST + P2_SECOND == 1 )) ||
	fail "p2 shown $P2_FIRST + $P2_SECOND times across restarts"
pass "own send h1 and sent carbon p2 not shown again (items 5, 6; p2 live=$P2_FIRST)"

# 11a. Forged result from a contact with a random queryid: dropped.
peer_bg "$WORK/forge.log" bob --stay 15 --do "sleep 4" \
	--do "raw <message to='alice@localhost/harness'><result xmlns='urn:xmpp:mam:2' queryid='random' id='f1'><forwarded xmlns='urn:xmpp:forward:0'><message xmlns='jabber:client' type='chat' from='bob@localhost/x' to='alice@localhost'><body>forged-1</body></message></forwarded></result></message>"
FORGE_PID=$PEER_PID
run_harness alice <<SCRIPT
connect 30
expect-debug cat=xmpp2-mam timeout=15 text=forged MAM result
no-ev timeout=2 texteq=forged-1
disconnect
SCRIPT
kill "$FORGE_PID" 2>/dev/null || true
pass "forged MAM result dropped (item 11a)"

# 1. Cursor-only first sync: mam_initial_days=0 and a cloned account
# ([meta] initial=cursor-only) show nothing but store a cursor.
CUSERDIR="$WORK/carol-userdir"
run_peer bob --do "send carol@localhost old-1" --do "sleep 1" >/dev/null
HUSERDIR="$CUSERDIR" run_harness carol <<SCRIPT
pref i:mam_initial_days=0
connect 30
expect-debug cat=xmpp2-mam text=account archive LIVE
sleep 2
disconnect
SCRIPT
# (mod_offline may still deliver old-1 live: that is not history)
CSTATE="$(find "$CUSERDIR/xmpp2" -name '*.state' | head -1)"
grep -q '^cursor=' "$CSTATE" || fail "cursor-only run stored no cursor"
# cloned: drop the cursor, mark cursor-only, new archived traffic
sed -i '/^cursor/d' "$CSTATE"
sed -i 's/^\[meta\]$/[meta]\ninitial=cursor-only/' "$CSTATE"
grep -q '^initial=cursor-only' "$CSTATE" || fail "could not mark the clone"
run_peer bob --do "send carol@localhost clone-1" --do "sleep 1" >/dev/null
# consume the offline copy first, as prpl-jabber would have shown it
run_peer carol --do "wait clone-1 10" >/dev/null
HUSERDIR="$CUSERDIR" run_harness carol <<SCRIPT
connect 30
expect-debug cat=xmpp2-mam text=account archive LIVE
no-ev timeout=3 kind=recv texteq=clone-1
disconnect
SCRIPT
grep -q '^cursor=' "$CSTATE" || fail "cloned account stored no cursor"
pass "cursor-only first sync (option 0 and cloned account) (item 1)"

# Removing the account deletes its state file ("account-removed" handler,
# connected after the core registers the signal: no GLib-CRITICAL).
HUSERDIR="$CUSERDIR" run_harness carol <<SCRIPT
remove-account
SCRIPT
[[ ! -e "$CSTATE" ]] || fail "state file kept after the account was removed"
# (other system prpls may log their own criticals: match ours only)
if grep -q 'signal_connect_common' "$H_ERR"; then
	fail "signal connected too early: $(grep -m1 signal_connect_common "$H_ERR")"
fi
pass "removing the account deletes its state file"
