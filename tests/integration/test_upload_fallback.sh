#!/usr/bin/env bash
# M7: a server without an upload service falls back to the inherited SI file
# transfer (no slot request, the peer sees an SI offer).
#   PRPL=prpl-xmpp2 PLUGIN_DIR=build tests/integration/test_upload_fallback.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$HERE/lib.sh"
export XMPP2_SERVER_PROFILE=noupload
test_init upload-fallback

head -c 65536 /dev/urandom >"$WORK/f.bin"

# The peer never answers the SI offer, so the xfer times out: the harness
# is expected to fail on "upload-fail"; what matters is what went on the wire.
peer_bg "$WORK/bob.log" bob --stay 30 --do "befriend alice@localhost" \
	--do "expect-stanza <si .*http://jabber.org/protocol/si 20"
BOB_PID=$PEER_PID
cat >"$WORK/alice.script" <<SCRIPT
connect 30
buddy-add bob@localhost
upload-fail bob@localhost $WORK/f.bin 6
SCRIPT
harness alice "$WORK/alice.script" --debug >"$WORK/alice.out" 2>"$WORK/alice.err" || true
grep -q "upload-fail: timeout" "$WORK/alice.out" || { cat "$WORK/alice.out"; fail "expected the SI offer to wait for the peer"; }
grep -q "xmpp2-upload" "$WORK/alice.err" && fail "the upload module acted without a service"
grep -q "http://jabber.org/protocol/si" "$WORK/alice.err" || fail "no SI offer was sent"
kill "$BOB_PID" 2>/dev/null || true
pass "no upload service: SI file transfer offered instead of an upload"
