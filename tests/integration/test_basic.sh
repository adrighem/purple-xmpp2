#!/usr/bin/env bash
# Baseline 1:1 chat: harness (alice) <-> slixmpp peer (bob), both directions.
# Defaults to the built-in prpl-jabber; run the plugin under test with:
#   PRPL=prpl-xmpp2 PLUGIN_DIR=build tests/integration/test_basic.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$HERE/lib.sh"

WORK="$(mktemp -d "$SCRATCH/basic-XXXXXX")"
cleanup() {
	jobs -p | xargs -r kill 2>/dev/null || true
	cd "${SCRATCH:?}" && rm -rf "./$(basename "$WORK")"
	if [[ -z "${XMPP2_KEEP_SERVER:-}" ]]; then
		stop_server
	fi
}
trap cleanup EXIT

ensure_venv
ensure_harness
start_server

TAG="$$-$RANDOM"
PING="ping-$TAG"
PONG="pong-$TAG"

# bob must be online before alice sends (server has no offline store).
peer bob --do "wait $PING" --do "send alice@localhost $PONG" \
	--do "sleep 1" >"$WORK/peer.log" 2>&1 &
PEER_PID=$!
wait_for_line "$WORK/peer.log" "PEER connected" 30 || { cat "$WORK/peer.log"; fail "peer did not connect"; }

cat >"$WORK/alice.script" <<SCRIPT
connect 30
send bob@localhost $PING
wait $PONG 20
count $PONG 1
disconnect
SCRIPT

echo "== prpl=$PRPL plugin_dir=${PLUGIN_DIR:-<system>}"
if ! harness alice "$WORK/alice.script" >"$WORK/alice.log" 2>"$WORK/alice.err"; then
	cat "$WORK/alice.log" "$WORK/alice.err" "$WORK/peer.log"
	fail "harness script failed"
fi
cat "$WORK/alice.log"
wait "$PEER_PID" || { cat "$WORK/peer.log"; fail "peer script failed"; }
grep -q "PEER recv chat .*: $PING" "$WORK/peer.log" || fail "peer never received $PING"
pass "alice->bob and bob->alice 1:1 chat via $PRPL"
