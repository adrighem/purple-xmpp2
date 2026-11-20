#!/usr/bin/env bash
# XEP-0363 streaming upload over https. Prosody serves its upload URLs over
# TLS with the test certificate (XMPP2_UPLOAD_HTTPS=1); the harness (alice)
# uploads without the allow_plain_http_upload option, so only https works.
# Checks: the file arrives byte for byte, progress is reported over TLS, the
# connection is made by purple_ssl_connect() to the URL host (the test
# certificate is trusted for that name only, through the harness' tls_peers
# pool, which is libpurple's own verification). A certificate that is not
# trusted makes libpurple ask the user (a UI request), which this headless
# harness cannot answer while an upload waits; so the refusal is not tested.
#   PRPL=prpl-xmpp2 PLUGIN_DIR=build tests/integration/test_upload_https.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$HERE/lib.sh"
export XMPP2_UPLOAD_HTTPS=1
export XMPP2_UPLOAD_LIMIT=$((64 * 1024 * 1024))
test_init upload-https

MIB=$((1024 * 1024))
SIZE=$((20 * MIB))

truncate -s "$SIZE" "$WORK/tls.bin"
head -c 65536 /dev/urandom | dd of="$WORK/tls.bin" conv=notrunc status=none
head -c 65536 /dev/urandom | dd of="$WORK/tls.bin" bs=65536 seek=$((SIZE / 65536 - 1)) conv=notrunc status=none
SHA_LOCAL="$(sha256sum "$WORK/tls.bin" | cut -d' ' -f1)"

# 1. End to end over TLS, no plain http option.
peer_bg "$WORK/bob.log" bob --stay 60 --do "wait https:// 50"
BOB_PID=$PEER_PID
run_harness alice --debug <<SCRIPT
connect 30
rss-reset
upload bob@localhost $WORK/tls.bin 50
rss-check 16
expect-debug cat=xmpp2-upload text=PUT $SIZE bytes to localhost (https)
expect-debug cat=xmpp2-upload text=upload done
disconnect
SCRIPT
wait_for_line "$WORK/bob.log" "PEER recv chat" 20 || { cat "$WORK/bob.log"; fail "peer got no message"; }
URL="$(sed -n 's/^PEER recv chat [^ ]*: \(https[^ ]*\)$/\1/p' "$WORK/bob.log" | head -1)"
[[ "$URL" == https://localhost:"$XMPP_HTTPS_PORT"/* ]] || { cat "$WORK/bob.log"; fail "body is not the https URL: '$URL'"; }
kill "$BOB_PID" 2>/dev/null || true
SHA_REMOTE="$(curl -sS --fail --cacert "$SCRATCH/server.crt" "$URL" | sha256sum | cut -d' ' -f1)"
[[ "$SHA_LOCAL" == "$SHA_REMOTE" ]] || fail "GET sha256 $SHA_REMOTE != $SHA_LOCAL"
STEPS="$(grep -c '^EV xfer-progress' "$H_OUT")"
[[ "$STEPS" -ge 20 ]] || fail "only $STEPS progress updates over TLS"
grep -q "^EV xfer-progress bytes=$SIZE size=$SIZE" "$H_OUT" || fail "progress did not reach the size"
pass "20 MiB over https: byte for byte, $STEPS progress updates, flat memory ($(grep '^OK rss-check' "$H_OUT" | sed 's/.*: //'))"
