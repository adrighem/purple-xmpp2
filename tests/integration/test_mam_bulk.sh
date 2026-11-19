#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# M3: MAM paging, live messages during catch-up and the page cap
# (PLAN M3 items 8, 9, 16).
# Run: PRPL=prpl-xmpp2 PLUGIN_DIR=build tests/integration/test_mam_bulk.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$HERE/lib.sh"
[[ "$PRPL" == "prpl-xmpp2" ]] || { echo "SKIP: needs PRPL=prpl-xmpp2"; exit 77; }
test_init mam-bulk

HUSERDIR="$WORK/alice-userdir"
export HUSERDIR

# check_sequence PREFIX FIRST LAST: the recv EV lines PREFIX-N in $H_OUT are
# exactly FIRST..LAST, each once, in order.
check_sequence() {
	local prefix="$1" first="$2" last="$3" got
	got="$(sed -n "s/^EV recv .* $prefix-\([0-9]*\)\$/\1/p" "$H_OUT" | tr '\n' ' ')"
	[[ "$got" == "$(seq -s ' ' "$first" "$last") " ]] ||
		fail "$prefix sequence: expected $first..$last, got ${got:0:200}..."
}

# Establish a cursor so later runs page with after=<cursor>.
run_harness alice <<SCRIPT
connect 30
expect-debug cat=xmpp2-mam text=account archive LIVE
disconnect
SCRIPT

# 8+9. 250 messages while offline; "live1" sent while the catch-up runs.
run_peer bob --do "sendmany alice@localhost 250 b" --do "sleep 1" >/dev/null
peer_bg "$WORK/live.log" bob --stay 20 --do "sleep 3" --do "send alice@localhost live1"
LIVE_PID=$PEER_PID
run_harness alice <<SCRIPT
connect 30
expect-ev kind=recv timeout=30 texteq=b-250
expect-ev kind=recv timeout=20 texteq=live1
sleep 2
count-ev kind=recv n=1 texteq=live1
count-debug cat=xmpp2-mam n=3 text=MAM page
disconnect
SCRIPT
kill "$LIVE_PID" 2>/dev/null || true
check_sequence b 1 250
last_b="$(grep -n '^EV recv .* b-250$' "$H_OUT" | cut -d: -f1)"
live="$(grep -n '^EV recv .* live1$' "$H_OUT" | cut -d: -f1)"
(( live > last_b )) || fail "live1 shown before the end of the catch-up"
pass "250 offline messages in 3 pages, in order, once; live1 after them (items 8, 9)"

# 16. 2500 messages: page cap, newest page shown, one notice.
run_peer bob --do "sendmany alice@localhost 2500 c" --do "sleep 2" >/dev/null
run_harness alice --debug <<SCRIPT
connect 30
expect-ev kind=recv timeout=120 texteq=c-2500
expect-debug cat=xmpp2-mam timeout=30 text=account archive LIVE
sleep 2
count-ev kind=system conv=bob@localhost n=1 text=Older history skipped
disconnect
SCRIPT
pages="$(grep -o 'MAM page [0-9]*' "$H_ERR" | sort -u | wc -l)"
(( pages >= 1 && pages <= 20 )) || fail "$pages pages fetched, cap is 20"
newest="$(sed -n 's/^EV recv .* c-\([0-9]*\)$/\1/p' "$H_OUT" | tail -100 | tr '\n' ' ')"
[[ "$newest" == "$(seq -s ' ' 2401 2500) " ]] ||
	fail "newest page not shown in order: ${newest:0:200}"
shown="$(grep -c '^EV recv .* c-[0-9]*$' "$H_OUT" || true)"
pass "page cap: $pages pages, $shown of 2500 shown, newest page last, one notice (item 16)"
