#!/usr/bin/env bash
# XEP-0363 streaming upload: a 50 MiB file goes from disk to the server in
# chunks. The harness (alice) uploads a sparse file, a slixmpp peer (carol)
# downloads it. Checks: identical bytes, progress reported to the PurpleXfer
# (growing, ending at the size), and the harness' peak RSS stays far below
# the file size (VmHWM, through /proc; v0.1.0 read the whole file). Needs the
# plugin:   PRPL=prpl-xmpp2 PLUGIN_DIR=build tests/integration/test_upload_stream.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$HERE/lib.sh"
# The test server's default limit is 10 MiB.
export XMPP2_UPLOAD_LIMIT=$((64 * 1024 * 1024))
test_init upload-stream

OPT=(--pref b:allow_plain_http_upload=1)
SIZE=$((50 * 1024 * 1024))
RSS_LIMIT_MB=16

# Sparse (no disk use), with data at the start, middle and end so that
# a chunk that is lost, repeated or moved changes the hash.
truncate -s "$SIZE" "$WORK/big.bin"
head -c 4096 /dev/urandom | dd of="$WORK/big.bin" conv=notrunc status=none
head -c 100000 /dev/urandom | dd of="$WORK/big.bin" bs=1 seek=$((25 * 1024 * 1024 + 7)) conv=notrunc status=none
head -c 4096 /dev/urandom | dd of="$WORK/big.bin" bs=1 seek=$((SIZE - 4096)) conv=notrunc status=none
SHA_LOCAL="$(sha256sum "$WORK/big.bin" | cut -d' ' -f1)"

peer_bg "$WORK/bob.log" bob --stay 90 --do "wait http:// 80"
BOB_PID=$PEER_PID
run_harness alice "${OPT[@]}" --debug <<SCRIPT
connect 30
rss-reset
upload bob@localhost $WORK/big.bin 80
rss-check $RSS_LIMIT_MB
expect-debug cat=xmpp2-upload text=upload done
disconnect
SCRIPT
wait_for_line "$WORK/bob.log" "PEER recv chat" 20 || { cat "$WORK/bob.log"; fail "peer got no message"; }
pass "50 MiB uploaded, peak RSS grew by less than $RSS_LIMIT_MB MiB ($(grep '^OK rss-check' "$H_OUT" | sed 's/.*: //'))"

# Progress: lines "EV xfer-progress bytes=N size=M", N never goes down,
# starts at 0 (connected), ends at the size, and there are many steps.
PROG="$WORK/progress.txt"
sed -n 's/^EV xfer-progress bytes=\([0-9]*\) size=\([0-9]*\)$/\1 \2/p' "$H_OUT" >"$PROG"
STEPS="$(wc -l <"$PROG")"
[[ "$STEPS" -ge 50 ]] || fail "only $STEPS progress updates for 50 MiB"
[[ "$(head -1 "$PROG")" == "0 $SIZE" ]] || fail "first progress is '$(head -1 "$PROG")'"
[[ "$(tail -1 "$PROG")" == "$SIZE $SIZE" ]] || fail "last progress is '$(tail -1 "$PROG")'"
awk 'NR > 1 && $1 < prev { bad = 1 } { prev = $1 } END { exit bad }' "$PROG" \
	|| fail "progress went backwards"
awk '$2 != '"$SIZE"' { bad = 1 } END { exit bad }' "$PROG" || fail "progress size changed"
pass "progress reported: $STEPS updates, 0 to $SIZE bytes, never backwards"

URL="$(sed -n 's/^PEER recv chat [^ ]*: \(http[^ ]*\)$/\1/p' "$WORK/bob.log" | head -1)"
[[ "$URL" == http://* ]] || { cat "$WORK/bob.log"; fail "body is not a URL: '$URL'"; }
SHA_REMOTE="$(run_peer carol --do "sha256 $URL" | sed -n 's/^PEER-SHA256 //p')"
[[ "$SHA_LOCAL" == "$SHA_REMOTE" ]] || fail "GET sha256 $SHA_REMOTE != $SHA_LOCAL"
kill "$BOB_PID" 2>/dev/null || true
pass "the server stored the 50 MiB file byte for byte (sha256 $SHA_LOCAL)"

# Debug log: our upload module never logs the slot URL or a header value (the
# XML console of the stream does show the slot IQ, as for any stanza).
if grep -E "xmpp2-upload|xmpp2-sendfile|util:" "$H_ERR" | grep -q -e "$URL" -e "file_share/"; then
	fail "the slot URL appears in the upload debug log"
fi
grep -q "xmpp2-upload: PUT 52428800 bytes to localhost (http)" "$H_ERR" || fail "no PUT log line"
pass "no slot URL in the upload log"
