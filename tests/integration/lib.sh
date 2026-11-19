#!/usr/bin/env bash
# Shared helpers for integration tests. Source this file; do not execute it.
# shellcheck shell=bash

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRATCH="${XMPP2_SCRATCH:-${TMPDIR:-/tmp}/xmpp2-scratch-$(id -u)}"
VENV="${XMPP2_VENV:-$SCRATCH/venv}"
HARNESS="${XMPP2_HARNESS:-$SCRATCH/xmpp2-harness}"
SERVER_SH="$ROOT/tests/server/run.sh"

PRPL="${PRPL:-prpl-jabber}"
PLUGIN_DIR="${PLUGIN_DIR:-}"
if [[ -n "$PLUGIN_DIR" ]]; then
	PLUGIN_DIR="$(cd "$PLUGIN_DIR" && pwd)"
fi

mkdir -p "$SCRATCH"

ensure_venv() {
	if [[ ! -x "$VENV/bin/python" ]]; then
		python3 -m venv "$VENV"
		"$VENV/bin/pip" -q --disable-pip-version-check install -r "$ROOT/tests/integration/requirements.txt"
	fi
}

ensure_harness() {
	local src="$ROOT/tests/integration/harness.c"
	if [[ ! -x "$HARNESS" || "$src" -nt "$HARNESS" ]]; then
		# shellcheck disable=SC2046
		gcc -Wall -Wextra -Wno-unused-parameter -o "$HARNESS" "$src" \
			$(pkg-config --cflags --libs purple glib-2.0)
	fi
}

# start_server: (re)start Prosody and load XMPP_* variables.
start_server() {
	"$SERVER_SH" stop >/dev/null
	"$SERVER_SH" start >"$SCRATCH/server.env"
	# shellcheck disable=SC1090,SC1091
	source "$SCRATCH/server.env"
	"$SERVER_SH" cert >"$SCRATCH/server.crt"
}

stop_server() {
	"$SERVER_SH" stop >/dev/null 2>&1 || true
}

# peer USER ARGS...: run the slixmpp peer as USER@localhost.
peer() {
	local user="$1"
	shift
	"$VENV/bin/python" "$ROOT/tests/integration/peer.py" \
		--jid "${user}@localhost/peer" --password "$XMPP_PASSWORD" \
		--port "$XMPP_C2S_PORT" "$@"
}

# harness USER SCRIPTFILE [EXTRA ARGS...]: run xmpp2-harness as USER@localhost.
harness() {
	local user="$1" script="$2"
	shift 2
	local args=(--prpl "$PRPL" --user "${user}@localhost/harness"
		--password "$XMPP_PASSWORD" --host 127.0.0.1 --port "$XMPP_C2S_PORT"
		--script "$script" --trust-cert "$SCRATCH/server.crt")
	if [[ -n "$PLUGIN_DIR" ]]; then
		args+=(--plugin-dir "$PLUGIN_DIR")
	fi
	"$HARNESS" "${args[@]}" "$@"
}

# wait_for_line FILE PATTERN [SECS]
wait_for_line() {
	local file="$1" pat="$2" secs="${3:-20}" _
	for _ in $(seq 1 $((secs * 10))); do
		if grep -q -- "$pat" "$file" 2>/dev/null; then
			return 0
		fi
		sleep 0.1
	done
	echo "timeout waiting for '$pat' in $file" >&2
	return 1
}

pass() { echo "PASS: $*"; }
fail() { echo "FAIL: $*" >&2; exit 1; }
