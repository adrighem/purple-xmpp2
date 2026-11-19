#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# M4: room without MAM keeps the legacy MUC history (PLAN M4 item 4).
# Server profile nomam (no mod_mam, no muc_mam).
# Run: PRPL=prpl-xmpp2 PLUGIN_DIR=build tests/integration/test_mam_muc_legacy.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$HERE/lib.sh"
[[ "$PRPL" == "prpl-xmpp2" ]] || { echo "SKIP: needs PRPL=prpl-xmpp2"; exit 77; }
export XMPP2_SERVER_PROFILE=nomam
test_init mam_muc_legacy

ROOM=m4legacy@conference.localhost

run_peer carol --do "join $ROOM carol" --do "groupsend $ROOM l1" \
	--do "sleep 1" --do "groupsend $ROOM l2" --do "sleep 1" \
	--do "leave $ROOM" >/dev/null
run_harness alice --debug <<SCRIPT
connect 30
join $ROOM alice
expect-debug cat=xmpp2-mam text=room $ROOM joined: no MAM, legacy history
expect-next kind=recv conv=$ROOM flag=DELAYED timeout=10 texteq=l1
expect-next kind=recv conv=$ROOM flag=DELAYED texteq=l2
no-debug cat=xmpp2-mam text=joining without legacy history
no-debug cat=xmpp2-mam text=room archive
leave $ROOM
sleep 1
join $ROOM alice
sleep 3
count-ev kind=recv conv=$ROOM n=2 texteq=l1
disconnect
SCRIPT
grep -q "maxstanzas='0'" "$H_ERR" && fail "legacy room joined with maxstanzas='0'"
pass "room without MAM: legacy history on every join, unchanged (item 4)"
