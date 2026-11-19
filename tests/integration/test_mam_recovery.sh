#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# M3: MAM failure paths (PLAN M3 items 10, 12, 14, 15, 17): corrupt cursor,
# watchdog, teardown during the catch-up, server without MAM.
# XMPP2_TEST_MAM_STALL=1 makes the plugin build but never send its MAM
# query (test hook in src/ext/mam_sync.c), so the archive stays SYNCING.
# Run: PRPL=prpl-xmpp2 PLUGIN_DIR=build tests/integration/test_mam_recovery.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$HERE/lib.sh"
[[ "$PRPL" == "prpl-xmpp2" ]] || { echo "SKIP: needs PRPL=prpl-xmpp2"; exit 77; }
test_init mam-recovery

HUSERDIR="$WORK/alice-userdir"
HLOG=txt
export HUSERDIR HLOG

# 10+17. Cursor at k1 (k0 two seconds older), then corrupted.
peer_bg "$WORK/k.log" bob --stay 20 --do "sleep 3" --do "send alice@localhost k0" \
	--do "sleep 2" --do "send alice@localhost k1"
K_PID=$PEER_PID
run_harness alice <<SCRIPT
connect 30
expect-ev kind=recv timeout=15 texteq=k1
sleep 3
disconnect
SCRIPT
kill "$K_PID" 2>/dev/null || true
STATE="$(find "$HUSERDIR/xmpp2" -name '*.state' | head -1)"
grep -q '^cursor_ts=' "$STATE" || fail "no cursor_ts in $STATE"
sed -i 's/^cursor=.*/cursor=no-such-id/' "$STATE"
run_peer bob --do "send alice@localhost n1" --do "sleep 1" >/dev/null
run_harness alice <<SCRIPT
connect 30
expect-debug cat=xmpp2-mam timeout=20 text=account archive LIVE
expect-ev kind=recv timeout=5 texteq=n1
expect-debug cat=xmpp2-mam text=unknown to the archive, retrying
expect-debug cat=xmpp2-mam text=account archive LIVE (complete)
sleep 2
count-ev kind=recv n=1 texteq=n1
no-ev timeout=1 kind=recv texteq=k0
no-ev timeout=1 kind=error
disconnect
SCRIPT
K1="$(grep -c '^EV recv .* k1$' "$H_OUT" || true)"
(( K1 <= 1 )) || fail "k1 shown $K1 times"
grep -q '^cursor=no-such-id' "$STATE" && fail "cursor not repaired"
pass "corrupt cursor: start=cursor_ts retry, no pre-cursor duplicates, no error (items 10, 17; boundary k1=$K1)"

# 14. Watchdog: the query never leaves, a live message waits ~30 s, then
# shows with one "history incomplete" notice.
peer_bg "$WORK/w.log" bob --stay 50 --do "sleep 4" --do "send alice@localhost w1"
W_PID=$PEER_PID
XMPP2_TEST_MAM_STALL=1 run_harness alice <<SCRIPT
connect 30
expect-debug cat=xmpp2-mam timeout=10 text=queued during catch-up
no-ev timeout=15 kind=recv texteq=w1
expect-debug cat=xmpp2-mam timeout=30 text=watchdog
expect-ev kind=recv timeout=5 texteq=w1
count-ev kind=system n=1 text=may be incomplete
count-ev kind=recv n=1 texteq=w1
disconnect
SCRIPT
kill "$W_PID" 2>/dev/null || true
pass "watchdog: LIVE after 30 s, queued message shown, one notice (item 14)"

# 15. Teardown during the catch-up delivers the queue before going offline.
peer_bg "$WORK/t.log" bob --stay 30 --do "sleep 4" --do "send alice@localhost t1"
T_PID=$PEER_PID
XMPP2_TEST_MAM_STALL=1 run_harness alice <<SCRIPT
connect 30
expect-debug cat=xmpp2-mam timeout=10 text=queued during catch-up
no-ev timeout=1 kind=recv texteq=t1
disconnect
count-ev kind=recv n=1 texteq=t1
SCRIPT
kill "$T_PID" 2>/dev/null || true
pass "teardown during catch-up shows queued messages (item 15)"

# 12. Server without MAM: login and messages work as before.
XMPP2_SERVER_PROFILE=nomam start_server
run_peer bob --do "send alice@localhost nomam-offline" --do "sleep 1" >/dev/null
peer_bg "$WORK/nm.log" bob --stay 20 --do "wait nomam-ping 15" --do "send alice@localhost nomam-pong"
NM_PID=$PEER_PID
run_harness alice <<SCRIPT
connect 30
expect-debug cat=xmpp2-mam text=no MAM support
expect-ev kind=recv timeout=10 texteq=nomam-offline
send bob@localhost nomam-ping
expect-ev kind=recv timeout=15 texteq=nomam-pong
count-ev kind=recv n=1 texteq=nomam-offline
disconnect
SCRIPT
wait "$NM_PID" || fail "peer did not get nomam-ping"
pass "server without MAM: login and messages unchanged (item 12)"
