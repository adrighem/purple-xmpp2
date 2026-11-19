#!/usr/bin/env bash
# Server without carbons (PLAN M2): login works, no enable IQ is sent, and the
# carbons_settled hook still fires so MAM is not blocked.
#   PRPL=prpl-xmpp2 PLUGIN_DIR=build tests/integration/test_carbons_off.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$HERE/lib.sh"

if [[ "$PRPL" != "prpl-xmpp2" ]]; then
	echo "SKIP: carbons need prpl-xmpp2"
	exit 77
fi
export XMPP2_SERVER_PROFILE=nocarbons
test_init carbons-off

run_harness alice <<SCRIPT
connect 30
expect-debug cat=xmpp2-carbons text=server has no carbons
count-debug cat=xmpp2 n=1 text=hook carbons_settled
no-debug timeout=1 cat=xmpp2-carbons text=enabling carbons
disconnect
SCRIPT
pass "no carbons on server: login ok, no enable IQ, settled at once"
