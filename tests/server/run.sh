#!/usr/bin/env bash
# Manage the throwaway Prosody test server (podman).
# Usage: [XMPP2_SERVER_PROFILE=full|nocarbons|nomam|noupload|a,b] run.sh start|stop|reset|status|logs|cert
# Upload options: XMPP2_UPLOAD_LIMIT=BYTES (max-file-size, default 10 MiB),
# XMPP2_UPLOAD_HTTPS=1 (upload URLs are https on XMPP2_TEST_HTTPS_PORT).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NAME="${XMPP2_TEST_CONTAINER:-xmpp2-test-prosody}"
IMAGE="localhost/xmpp2-test-prosody"
C2S_PORT="${XMPP2_TEST_C2S_PORT:-15222}"
HTTP_PORT="${XMPP2_TEST_HTTP_PORT:-15280}"
HTTPS_PORT="${XMPP2_TEST_HTTPS_PORT:-$((HTTP_PORT + 1))}"
UPLOAD_LIMIT="${XMPP2_UPLOAD_LIMIT:-}"
UPLOAD_HTTPS="${XMPP2_UPLOAD_HTTPS:-}"
PASSWORD="${XMPP2_TEST_PASSWORD:-secret}"
PROFILE="${XMPP2_SERVER_PROFILE:-full}"
USERS=(alice bob carol)

# Profile -> features to disable (comma list, see prosody.cfg.lua).
case "$PROFILE" in
full) DISABLE="" ;;
nocarbons) DISABLE="carbons" ;;
nomam) DISABLE="mam" ;;
noupload) DISABLE="upload" ;;
*) DISABLE="$PROFILE" ;; # custom comma list, e.g. carbons,mam
esac

running() {
	[[ "$(podman inspect -f '{{.State.Running}}' "$NAME" 2>/dev/null || true)" == "true" ]]
}

build() {
	local out
	out="$(podman build -q -t "$IMAGE" -f "$HERE/Containerfile" "$HERE" 2>&1)" || { echo "$out" >&2; return 1; }
}

wait_ready() {
	for _ in $(seq 1 60); do
		if podman exec "$NAME" prosodyctl status >/dev/null 2>&1 \
			&& (exec 3<>"/dev/tcp/127.0.0.1/$C2S_PORT") 2>/dev/null; then
			return 0
		fi
		sleep 0.5
	done
	echo "prosody did not become ready" >&2
	podman logs "$NAME" >&2 || true
	return 1
}

start() {
	if running; then
		local cur
		cur="$(podman inspect -f '{{index .Config.Labels "xmpp2.profile"}}' "$NAME")"
		if [[ "$cur" == "$PROFILE/$UPLOAD_LIMIT/$UPLOAD_HTTPS" ]]; then
			info
			return 0
		fi
	fi
	podman rm -f "$NAME" >/dev/null 2>&1 || true
	build
	local extra=()
	if [[ -n "$UPLOAD_LIMIT" ]]; then
		extra+=(-e "XMPP2_UPLOAD_LIMIT=${UPLOAD_LIMIT}")
	fi
	if [[ -n "$UPLOAD_HTTPS" ]]; then
		extra+=(-e "XMPP2_HTTPS_PORT=${HTTPS_PORT}" -p "127.0.0.1:${HTTPS_PORT}:5281")
	fi
	podman run -d --name "$NAME" \
		--label "xmpp2.profile=${PROFILE}/${UPLOAD_LIMIT}/${UPLOAD_HTTPS}" \
		-e "XMPP2_DISABLE=${DISABLE}" -e "XMPP2_HTTP_PORT=${HTTP_PORT}" \
		-p "127.0.0.1:${C2S_PORT}:5222" \
		-p "127.0.0.1:${HTTP_PORT}:5280" \
		"${extra[@]}" \
		"$IMAGE" >/dev/null
	wait_ready
	local u
	for u in "${USERS[@]}"; do
		podman exec "$NAME" prosodyctl register "$u" localhost "$PASSWORD" >/dev/null
	done
	info
}

stop() {
	podman rm -f "$NAME" >/dev/null 2>&1 || true
	echo "stopped"
}

info() {
	echo "XMPP_PROFILE=${PROFILE}"
	echo "XMPP_HOST=localhost"
	echo "XMPP_DOMAIN=localhost"
	echo "XMPP_C2S_PORT=${C2S_PORT}"
	echo "XMPP_HTTP_PORT=${HTTP_PORT}"
	echo "XMPP_HTTPS_PORT=${HTTPS_PORT}"
	echo "XMPP_MUC=conference.localhost"
	echo "XMPP_UPLOAD=upload.localhost"
	echo "XMPP_USERS=\"${USERS[*]}\""
	echo "XMPP_PASSWORD=${PASSWORD}"
}

case "${1:-}" in
start) start ;;
stop) stop ;;
reset) stop; start ;;
status) if running; then info; else echo "not running"; exit 1; fi ;;
logs) podman logs "$NAME" ;;
cert) podman exec "$NAME" cat /etc/prosody/certs/localhost.crt ;;
*) echo "usage: $0 start|stop|reset|status|logs|cert" >&2; exit 2 ;;
esac
