#!/usr/bin/env bash
# XEP-0363 streaming upload under a misbehaving or slow upload server.
# The harness (alice) goes through tests/integration/upload_stub.py, an HTTP
# CONNECT proxy (so this also proves the account proxy is honoured): XMPP
# traffic is tunnelled to Prosody, the PUT of the slot is answered by the
# stub. Checks: 500 and a redirect fail with a clear message (the redirect is
# not followed), a 413 sent at once while we are still writing is read,
# backpressure keeps memory flat for a 50 MiB file, cancelling mid-upload
# (as the Cancel button does), going offline mid-upload and deleting the
# account mid-upload stop the transfer and close the connection.
#   PRPL=prpl-xmpp2 PLUGIN_DIR=build tests/integration/test_upload_errors.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$HERE/lib.sh"
export XMPP2_UPLOAD_LIMIT=$((64 * 1024 * 1024))
test_init upload-errors

OPT=(--pref b:allow_plain_http_upload=1)
MIB=$((1024 * 1024))

truncate -s $((1 * MIB)) "$WORK/small.bin"
truncate -s $((20 * MIB)) "$WORK/mid.bin"
truncate -s $((50 * MIB)) "$WORK/big.bin"

# stub_start MODE: start the proxy; sets STUB_PORT, STUB_PID and STUB_LOG.
stub_start() {
	STUB_LOG="$WORK/stub-$1.log"
	"$VENV/bin/python" "$HERE/upload_stub.py" --upload-port "$XMPP_HTTP_PORT" \
		--mode "$2" >"$STUB_LOG" 2>&1 &
	STUB_PID=$!
	wait_for_line "$STUB_LOG" "^PORT " 10 || { cat "$STUB_LOG"; fail "stub did not start"; }
	STUB_PORT="$(sed -n 's/^PORT //p' "$STUB_LOG")"
}

stub_stop() {
	kill "$STUB_PID" 2>/dev/null || true
	wait "$STUB_PID" 2>/dev/null || true
}

# via_stub NAME MODE: run the harness script on stdin through the stub.
via_stub() {
	stub_start "$1" "$2"
	run_harness alice "${OPT[@]}" --proxy "127.0.0.1:$STUB_PORT" --debug
}

# stub_put_bytes: bytes the stub received of its (last) PUT.
stub_put_bytes() {
	sed -n 's/^STUB put bytes=\([0-9]*\) .*/\1/p' "$STUB_LOG" | tail -1
}

# 1. A plain 500 after the whole body: failure with the code.
via_stub 500 status:500 <<SCRIPT
connect 30
upload-fail bob@localhost $WORK/small.bin 20
expect-debug cat=xmpp2-upload text=the upload server answered HTTP 500
no-debug timeout=1 cat=xmpp2-upload text=upload done
disconnect
SCRIPT
wait_for_line "$STUB_LOG" "STUB put bytes=$MIB of=$MIB result=500" 5 || { cat "$STUB_LOG"; fail "stub did not get the whole body"; }
stub_stop
pass "HTTP 500 after a 1 MiB PUT fails with the status"

# 2. A redirect is not followed (no second request, no plain http detour).
via_stub 302 status:302 <<SCRIPT
connect 30
upload-fail bob@localhost $WORK/small.bin 20
expect-debug cat=xmpp2-upload text=redirected the upload (HTTP 302), which is not supported
disconnect
SCRIPT
[[ "$(grep -c '^STUB put' "$STUB_LOG")" == 1 ]] || { cat "$STUB_LOG"; fail "redirect was followed"; }
stub_stop
pass "a 302 fails with its own message and is not followed"

# 3. 413 sent at once, while the 20 MiB body is still being written: the
# early answer is read, and the upload stops long before the end.
via_stub 413 early:413 <<SCRIPT
connect 30
upload-fail bob@localhost $WORK/mid.bin 20
expect-debug cat=xmpp2-upload text=too large for the upload server (HTTP 413)
disconnect
SCRIPT
# the last progress line: bytes written before the answer stopped us
GOT="$(sed -n 's/^EV xfer-progress bytes=\([0-9]*\) .*/\1/p' "$H_OUT" | tail -1)"
[[ -n "$GOT" && "$GOT" -lt $((20 * MIB)) ]] || fail "sent '$GOT' bytes to a server that said 413"
stub_stop
pass "413 answered while sending is reported, upload stopped after $GOT of $((20 * MIB)) bytes"

# 3b. A "201 Created" before the body is complete is not a success.
via_stub early201 early:201 <<SCRIPT
connect 30
upload-fail bob@localhost $WORK/mid.bin 20
expect-debug cat=xmpp2-upload text=answered before the whole file was sent
no-debug timeout=1 cat=xmpp2-upload text=upload done
disconnect
SCRIPT
stub_stop
pass "a 201 sent before the body is complete fails the upload"

# 4. Backpressure: the stub takes 16 MiB/s. The 50 MiB file goes through with
# the peak RSS flat, and the stub saw every byte.
via_stub slow slow:$((16 * MIB)) <<SCRIPT
connect 30
rss-reset
upload bob@localhost $WORK/big.bin 60
rss-check 16
expect-debug cat=xmpp2-upload text=upload done
disconnect
SCRIPT
wait_for_line "$STUB_LOG" "STUB put bytes=$((50 * MIB)) of=$((50 * MIB)) result=201" 5 || { cat "$STUB_LOG"; fail "stub did not get 50 MiB"; }
stub_stop
pass "50 MiB to a slow server: memory flat ($(grep '^OK rss-check' "$H_OUT" | sed 's/.*: //')), all bytes delivered"

# 5. Cancel from the transfer window after 2 MiB: the upload ends, the
# connection is closed, the server got a fraction of the file.
via_stub cancel slow:$((2 * MIB)) <<SCRIPT
connect 30
xfer-cancel-at $((2 * MIB))
upload-fail bob@localhost $WORK/big.bin 30
no-debug timeout=1 cat=xmpp2-upload text=upload done
disconnect
SCRIPT
wait_for_line "$STUB_LOG" "STUB put .* result=closed" 10 || { cat "$STUB_LOG"; fail "the connection stayed open after the cancel"; }
GOT="$(stub_put_bytes)"
[[ "$GOT" -lt $((25 * MIB)) ]] || fail "server received $GOT bytes after a cancel at 2 MiB"
stub_stop
pass "cancel mid-upload: connection closed after $GOT of $((50 * MIB)) bytes"

# 6. Going offline mid-upload, and 7. deleting the account mid-upload:
# stream_closing cancels the PUT (watchers and socket gone, no crash).
for how in disconnect remove; do
	via_stub "$how" slow:$((2 * MIB)) <<SCRIPT
connect 30
xfer-cancel-at $((2 * MIB)) $how
upload-fail bob@localhost $WORK/big.bin 30
SCRIPT
	wait_for_line "$STUB_LOG" "STUB put .* result=closed" 10 || { cat "$STUB_LOG"; fail "the connection stayed open ($how)"; }
	stub_stop
	if grep -q "still alive at stream_free" "$H_ERR"; then
		fail "upload outlived the stream ($how)"
	fi
	# (the GLib-CRITICAL lines at start-up come from other prpls' plugins)
	if grep -E "CRITICAL|WARNING \*\*" "$H_ERR" | grep -v "GLib-CRITICAL"; then
		fail "warnings during $how"
	fi
done
pass "sign-off and account removal mid-upload close the connection cleanly"

# 8. A server that stops reading and never answers: the inactivity watchdog
# fails the upload (60 s in production, 3 s through the test hook).
if test_hooks_enabled; then
	XMPP2_TEST_UPLOAD_STALL_SECS=3 via_stub stall stall:$((1 * MIB)) <<SCRIPT
connect 30
upload-fail bob@localhost $WORK/mid.bin 30
expect-debug cat=xmpp2-upload text=the upload stalled: no data moved for 3 seconds
no-debug timeout=1 cat=xmpp2-upload text=upload done
disconnect
SCRIPT
	stub_stop
	pass "a stalled upload server fails the transfer after the watchdog timeout"
else
	echo "SKIP: stall watchdog needs -DXMPP2_TEST_HOOKS=ON"
fi
