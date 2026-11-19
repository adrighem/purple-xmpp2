#!/usr/bin/env bash
# Self-test of the harness and peer extensions (EV lines, filters, userdir,
# logs, buddy, rooms, receipts, raw stanzas, upload). Feature-neutral: works
# with prpl-jabber and prpl-xmpp2. Feature tests (carbons, MAM ...) build on
# the same primitives; see tests/README.md.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$HERE/lib.sh"
test_init selftest

HUSERDIR="$WORK/alice-userdir"
HLOG=txt
export HUSERDIR HLOG

# 1. Peer online, harness exchanges messages: EV lines, filters, ordering.
peer_bg "$WORK/bob.log" bob --stay 60 --do "befriend alice@localhost" \
	--do "receipts on" --do "wait ping-1" --do "send alice@localhost pong-1" \
	--do "sendr alice@localhost pong-2" --do "send alice@localhost pong-3"
BOB_PID=$PEER_PID
run_harness alice <<SCRIPT
connect 30
buddy-add bob@localhost
send bob@localhost ping-1
expect-ev kind=sent conv=bob@localhost text=ping-1
expect-next kind=recv ctype=im text=pong-1
expect-next kind=recv who=bob@localhost text=pong-2
expect-next kind=recv text=pong-3
count-ev kind=recv n=3
no-ev timeout=1 kind=recv text=never-sent
wait pong-2
count pong-3 1
focus bob@localhost
logcount bob@localhost pong-1 1
logcount bob@localhost ping-1 1
disconnect
SCRIPT
grep -q '^EV recv conv=bob@localhost[^ ]* who=bob@localhost/peer flags=0x2 mtime=[0-9]* names=RECV ctype=im pong-1$' "$H_OUT" ||
	fail "EV line format"
kill "$BOB_PID" 2>/dev/null || true
pass "EV lines, expect-next ordering, counts, focus, text logs"

# 2. Persistent userdir: second run sees the logs of the first, state kept.
run_harness alice <<SCRIPT
logcount bob@localhost pong-1 1
SCRIPT
test -f "$HUSERDIR/accounts.xml" || fail "userdir not persisted"
pass "userdir persists across runs"

# 3. Offline send then reconnect (server side offline storage or MAM).
run_harness alice <<SCRIPT
connect 30
send bob@localhost kept-1
disconnect
SCRIPT
run_peer bob --do "send alice@localhost offline-1" --do "sleep 1" >/dev/null
run_harness alice <<SCRIPT
connect 30
expect-ev timeout=15 kind=recv text=offline-1
SCRIPT
pass "message sent while harness offline is delivered on reconnect"

# 4. Raw stanza, expect-stanza, receipts, debug capture.
peer_bg "$WORK/bob2.log" bob --stay 30 --do "receipts on" \
	--do "expect-stanza body>rawping" --do "raw <message to='alice@localhost' type='chat'><body>rawpong</body></message>"
BOB_PID=$PEER_PID
# Debug assertions only apply to our own xmpp2* categories.
DEBUG_CHECKS=""
if [[ "$PRPL" == "prpl-xmpp2" ]]; then
	DEBUG_CHECKS="expect-debug cat=xmpp2 text=rawpong
no-debug timeout=1 cat=xmpp2 text=this-never-appears"
fi
run_harness alice <<SCRIPT
connect 30
send bob@localhost rawping
expect-ev kind=recv text=rawpong
$DEBUG_CHECKS
disconnect
SCRIPT
kill "$BOB_PID" 2>/dev/null || true
# A live message is never shown as delayed, also not while the MAM
# catch-up of a fresh login returns the same message from the archive.
grep -q '^EV recv .* flags=0x2 .* rawpong$' "$H_OUT" ||
	fail "live rawpong shown as delayed"
pass "raw stanza, expect-stanza, debug capture"

# 5. MUC: peer and harness exchange room messages.
peer_bg "$WORK/carol.log" carol --stay 40 --do "join selftest@conference.localhost carolnick" \
	--do "groupwait hello-room" --do "groupsend selftest@conference.localhost hello-back"
CAROL_PID=$PEER_PID
run_harness alice <<SCRIPT
connect 30
join selftest@conference.localhost alicenick
groupsend selftest@conference.localhost hello-room
expect-ev ctype=chat kind=recv who=carolnick text=hello-back
leave selftest@conference.localhost
disconnect
SCRIPT
kill "$CAROL_PID" 2>/dev/null || true
pass "MUC join, groupsend, receive, leave"

# 6. Upload plumbing on the peer side (server component, PUT, GET, sha256).
head -c 200000 /dev/urandom >"$WORK/blob.bin"
SHA_LOCAL="$(sha256sum "$WORK/blob.bin" | cut -d' ' -f1)"
OUT="$(run_peer bob --do "upload $WORK/blob.bin" 2>&1)"
URL="$(sed -n 's/^PEER-URL //p' <<<"$OUT")"
[[ -n "$URL" ]] || fail "peer upload gave no URL: $OUT"
SHA_REMOTE="$(run_peer bob --do "sha256 $URL" | sed -n 's/^PEER-SHA256 //p')"
[[ "$SHA_LOCAL" == "$SHA_REMOTE" ]] || fail "upload sha256 mismatch"
pass "peer upload and download round trip ($URL)"
