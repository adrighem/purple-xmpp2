#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# M4: room archive page cap, stale cursor and watchdog (PLAN M4 items 3, 9;
# ARCHITECTURE 7.2 watchdog for a room).
# Run: PRPL=prpl-xmpp2 PLUGIN_DIR=build tests/integration/test_mam_muc_bulk.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$HERE/lib.sh"
[[ "$PRPL" == "prpl-xmpp2" ]] || { echo "SKIP: needs PRPL=prpl-xmpp2"; exit 77; }
test_init mam_muc_bulk

HUSERDIR="$WORK/alice-userdir"
export HUSERDIR
ROOM=m4bulk@conference.localhost

# age_cursor: make the room cursor older than mam_muc_threshold_days (2)
age_cursor() {
	local state old
	state="$(find "$HUSERDIR/xmpp2" -name '*.state' | head -1)"
	[[ -n "$state" ]] || fail "no state file"
	grep -q "^\[archive $ROOM\]" "$state" || fail "no room cursor in $state"
	old=$(( $(date +%s) - 3 * 24 * 3600 ))
	sed -i "/^\[archive $ROOM\]/,/^\[/ s/^cursor_ts=.*/cursor_ts=$old/" \
		"$state"
}

# A first join stores the room cursor.
run_peer carol --do "join $ROOM carol" --do "groupsend $ROOM seed" \
	--do "sleep 1" --do "leave $ROOM" >/dev/null
run_harness alice <<SCRIPT
connect 30
join $ROOM alice
expect-ev kind=recv conv=$ROOM timeout=15 texteq=seed
expect-debug cat=xmpp2-mam text=room archive $ROOM LIVE
disconnect
SCRIPT

# 9. Fresh cursor, 600 messages behind (12 pages of 50, cap 10 pages):
# pages 1 to 9 shown, then one notice and the newest page.
run_peer carol --do "join $ROOM carol" \
	--do "groupsendmany $ROOM 600 b" --do "sleep 2" \
	--do "leave $ROOM" >/dev/null
run_harness alice <<SCRIPT
connect 30
join $ROOM alice
expect-debug cat=xmpp2-mam timeout=60 text=room archive $ROOM LIVE
expect-next kind=recv conv=$ROOM texteq=b-1
expect-next kind=recv conv=$ROOM texteq=b-450
expect-next kind=system conv=$ROOM text=Older history skipped
expect-next kind=recv conv=$ROOM texteq=b-551
expect-next kind=recv conv=$ROOM texteq=b-600
count-ev kind=recv conv=$ROOM n=500
count-ev kind=system n=1 text=Older history skipped
no-ev timeout=1 texteq=b-451
count-debug cat=xmpp2-mam n=10 text=to $ROOM:
disconnect
SCRIPT
pass "600 behind: 9 pages, one notice, newest page (item 9)"

# 3. Cursor older than the threshold, 60 new messages: the last 50 only,
# with one notice.
run_peer carol --do "join $ROOM carol" \
	--do "groupsendmany $ROOM 60 c" --do "sleep 2" \
	--do "leave $ROOM" >/dev/null
age_cursor
run_harness alice <<SCRIPT
connect 30
join $ROOM alice
expect-debug cat=xmpp2-mam timeout=30 text=room archive $ROOM LIVE
expect-next kind=system conv=$ROOM text=Older history skipped
expect-next kind=recv conv=$ROOM texteq=c-11
expect-next kind=recv conv=$ROOM texteq=c-60
count-ev kind=recv conv=$ROOM n=50
count-ev kind=system n=1 text=Older history skipped
no-ev timeout=1 texteq=c-10
disconnect
SCRIPT
pass "stale cursor: last 50 messages and one notice (item 3)"

# 3 (overlap). Stale cursor that the last page still reaches: only the
# messages after it, no notice.
run_peer carol --do "join $ROOM carol" \
	--do "groupsendmany $ROOM 3 d" --do "sleep 2" \
	--do "leave $ROOM" >/dev/null
age_cursor
run_harness alice <<SCRIPT
connect 30
join $ROOM alice
expect-debug cat=xmpp2-mam timeout=30 text=room archive $ROOM LIVE
expect-next kind=recv conv=$ROOM texteq=d-1
expect-next kind=recv conv=$ROOM texteq=d-3
sleep 1
count-ev kind=recv conv=$ROOM n=3
no-ev timeout=1 kind=system text=Older history skipped
disconnect
SCRIPT
pass "stale cursor inside the last page: no repeats, no notice (item 3)"

test_hooks_enabled || { echo "SKIP: room watchdog needs -DXMPP2_TEST_HOOKS=ON"; exit 77; }

# Last page with more results than max (XMPP2_TEST_HOOKS lowers the limit the
# client enforces to 20, the server sends the 50 it was asked for): the
# first 20 are kept, the other 30 are dropped as forged.
run_peer carol --do "join $ROOM carol" \
	--do "groupsendmany $ROOM 60 e" --do "sleep 2" \
	--do "leave $ROOM" >/dev/null
age_cursor
XMPP2_TEST_MAM_PAGE_LIMIT=20 run_harness alice <<SCRIPT
connect 30
join $ROOM alice
expect-debug cat=xmpp2-mam timeout=30 text=room archive $ROOM LIVE
count-ev kind=recv conv=$ROOM n=20 text=e-
count-debug cat=xmpp2-mam n=30 text=more results than the page size
no-ev timeout=1 texteq=e-31
disconnect
SCRIPT
pass "last page: results beyond max are dropped, not buffered"

# Watchdog (XMPP2_TEST_HOOKS): the room query is never answered; after about 30 s the room
# goes LIVE, the queued live message appears after one notice.
peer_bg "$WORK/live.log" carol --stay 45 --do "join $ROOM carol" \
	--do "sleep 12" --do "groupsend $ROOM w-live"
LIVE_PID=$PEER_PID
XMPP2_TEST_MAM_STALL=1 run_harness alice <<SCRIPT
connect 30
join $ROOM alice
expect-debug cat=xmpp2-mam timeout=20 text=queued during catch-up
no-ev timeout=5 texteq=w-live
expect-debug cat=xmpp2-mam timeout=40 text=room archive $ROOM LIVE (incomplete)
expect-next kind=system conv=$ROOM text=history may be incomplete
expect-next kind=recv conv=$ROOM texteq=w-live
count-ev kind=system conv=$ROOM n=1 text=history may be incomplete
disconnect
SCRIPT
kill "$LIVE_PID" 2>/dev/null || true
pass "room watchdog: queued live message shown after one notice"
