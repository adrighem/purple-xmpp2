#!/usr/bin/env bash
# M7 HTTP File Upload (XEP-0363): harness (alice) sends files, slixmpp peers
# (bob, carol) receive the URL and download it. Needs the plugin:
#   PRPL=prpl-xmpp2 PLUGIN_DIR=build tests/integration/test_upload.sh
# The test server serves plain http, so the account option that allows
# unencrypted upload URLs is switched on (it is off by default).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$HERE/lib.sh"
test_init upload

OPT=(--pref b:allow_plain_http_upload=1)
MUC=upload-room@conference.localhost

head -c 1048576 /dev/urandom >"$WORK/one.png"
SHA_LOCAL="$(sha256sum "$WORK/one.png" | cut -d' ' -f1)"

# 1. 1:1: URL body plus x:oob, GET returns identical bytes.
peer_bg "$WORK/bob.log" bob --stay 40 --do "expect-stanza <x xmlns=.jabber:x:oob.><url>http 25" \
	--do "wait http:// 25"
BOB_PID=$PEER_PID
run_harness alice "${OPT[@]}" <<SCRIPT
connect 30
upload bob@localhost $WORK/one.png 30
expect-ev kind=sent conv=bob@localhost text=http
expect-debug cat=xmpp2-upload text=upload done
disconnect
SCRIPT
wait_for_line "$WORK/bob.log" "PEER recv chat" 20 || { cat "$WORK/bob.log"; fail "peer got no message"; }
URL="$(sed -n 's/^PEER recv chat [^ ]*: \(http[^ ]*\)$/\1/p' "$WORK/bob.log" | head -1)"
[[ "$URL" == http://* || "$URL" == https://* ]] || { cat "$WORK/bob.log"; fail "body is not a URL: '$URL'"; }
[[ "$URL" == *one.png ]] || fail "URL does not end in the file name: $URL"
SHA_REMOTE="$(run_peer carol --do "sha256 $URL" | sed -n 's/^PEER-SHA256 //p')"
[[ "$SHA_LOCAL" == "$SHA_REMOTE" ]] || fail "GET sha256 $SHA_REMOTE != $SHA_LOCAL"
kill "$BOB_PID" 2>/dev/null || true
pass "1 MiB upload to a contact: URL body, x:oob, identical bytes ($URL)"

# 2. Room: members receive the URL.
peer_bg "$WORK/carol.log" carol --stay 40 --do "join $MUC carolnick" \
	--do "groupwait http:// 25" --do "expect-stanza <x xmlns=.jabber:x:oob. 5"
CAROL_PID=$PEER_PID
run_harness alice "${OPT[@]}" <<SCRIPT
connect 30
join $MUC alicenick
upload $MUC $WORK/one.png 30
disconnect
SCRIPT
wait_for_line "$WORK/carol.log" "PEER recv groupchat" 20 || { cat "$WORK/carol.log"; fail "room member got no URL"; }
grep -q 'PEER recv groupchat .*: http.*one.png' "$WORK/carol.log" || { cat "$WORK/carol.log"; fail "room URL malformed"; }
kill "$CAROL_PID" 2>/dev/null || true
pass "upload to a room reaches the members"

# 3. Over the limit (test server: 10 MiB): fails before any request, clear error.
head -c 11534336 /dev/zero >"$WORK/big.bin"
run_harness alice "${OPT[@]}" <<SCRIPT
connect 30
upload-fail bob@localhost $WORK/big.bin 15
expect-debug cat=xmpp2-upload text=can be uploaded to this server
no-debug timeout=1 cat=xmpp2-upload text=requested an upload slot
disconnect
SCRIPT
pass "file above max-file-size is refused locally with a clear message"

# 4. Plain http offered but the account option is off: refused with advice.
run_harness alice <<SCRIPT
connect 30
upload-fail bob@localhost $WORK/one.png 15
expect-debug cat=xmpp2-upload text=Allow unencrypted file upload
disconnect
SCRIPT
pass "unencrypted upload URL refused unless the account allows it"

# 5. Discovery: the service comes from our own server's disco#items.
run_harness alice "${OPT[@]}" <<SCRIPT
connect 30
expect-debug cat=xmpp2-upload text=upload service upload.localhost
disconnect
SCRIPT
pass "upload service discovered from our own server's items only"

# 6. Connection lost while the slot request is in flight: the transfer is
# cancelled while the stream still has its IQ table (a use-after-free
# before; run under -DXMPP2_SANITIZE=ON to see it), no crash, no criticals.
# Resumption off (sm_resume=0): with it the drop is resumed and the upload
# completes, which test_sm_resume.sh covers for messages.
run_harness alice "${OPT[@]}" --pref b:sm_resume=0 <<SCRIPT
connect 30
upload-drop bob@localhost $WORK/one.png 15
expect-debug cat=xmpp2-upload text=requested an upload slot
no-debug timeout=1 cat=xmpp2-upload text=upload done
SCRIPT
if grep -q "still alive at stream_free" "$H_ERR"; then
	fail "upload outlived the stream"
fi
pass "upload cancelled cleanly when the connection drops mid-request"
