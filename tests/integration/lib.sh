#!/usr/bin/env bash
# Shared helpers for integration tests. Source this file; do not execute it.
# shellcheck shell=bash

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# One scratch dir per server container: server.env, server.crt and the venv
# of concurrent runs with different XMPP2_TEST_CONTAINER must not collide.
SCRATCH="${XMPP2_SCRATCH:-${TMPDIR:-/tmp}/xmpp2-scratch-$(id -u)-${XMPP2_TEST_CONTAINER:-xmpp2-test-prosody}}"
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
	local dir="$ROOT/tests/integration" f stale=0
	local srcs=("$dir"/harness*.c)
	if [[ ! -x "$HARNESS" ]]; then
		stale=1
	else
		for f in "${srcs[@]}" "$dir"/harness.h; do
			[[ "$f" -nt "$HARNESS" ]] && stale=1
		done
	fi
	if [[ $stale == 1 ]]; then
		# shellcheck disable=SC2046
		gcc -Wall -Wextra -Wno-unused-parameter -o "$HARNESS" "${srcs[@]}" \
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
# Resource defaults to "peer"; set PEER_RESOURCE=phone for alice/phone etc.
peer() {
	local user="$1"
	shift
	"$VENV/bin/python" "$ROOT/tests/integration/peer.py" \
		--jid "${user}@localhost" --resource "${PEER_RESOURCE:-peer}" \
		--password "$XMPP_PASSWORD" --port "$XMPP_C2S_PORT" "$@"
}

# peer_bg LOGFILE USER ARGS...: run a peer in the background, wait for it to
# connect, and leave its pid in PEER_PID. Use "--stay N" to keep it online.
peer_bg() {
	local log="$1"
	shift
	peer "$@" >"$log" 2>&1 &
	# shellcheck disable=SC2034 # read by callers
	PEER_PID=$!
	wait_for_line "$log" "PEER connected" 30 || { cat "$log" >&2; return 1; }
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
	# HUSERDIR: persistent --userdir; HLOG=txt enables conversation logging.
	if [[ -n "${HUSERDIR:-}" ]]; then
		args+=(--userdir "$HUSERDIR")
	fi
	if [[ -n "${HLOG:-}" ]]; then
		args+=(--log "$HLOG")
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

# pidgin_xvfb SECS CONFDIR LOG: run Pidgin under Xvfb for SECS seconds with
# CONFDIR (set HOME and XDG_* first), debug output to LOG. Pidgin's SIGTERM
# handler is not async-signal-safe and may hang: it gets SIGKILL 5 s after
# the TERM, and the outer timeout stays above that, or xvfb-run would be
# killed instead and leave Pidgin writing into CONFDIR. Returns once no
# Pidgin of CONFDIR is left (fails loud if one survives), so the caller may
# remove the directory.
pidgin_xvfb() {
	local secs="$1" conf="$2" log="$3" pat _
	timeout "$((secs + 30))" xvfb-run -a \
		timeout -k 5 "$secs" pidgin -n -d -c "$conf" >"$log" 2>&1 || true
	pat="^pidgin -n -d -c $conf\$"
	for _ in $(seq 1 50); do
		pgrep -u "$(id -u)" -f -- "$pat" >/dev/null || return 0
		sleep 0.1
	done
	echo "pidgin for $conf still running, killing it" >&2
	pkill -KILL -u "$(id -u)" -f -- "$pat" || true
	for _ in $(seq 1 50); do
		pgrep -u "$(id -u)" -f -- "$pat" >/dev/null || return 0
		sleep 0.1
	done
	echo "pidgin for $conf does not exit" >&2
	return 1
}

# test_hooks_enabled: the plugin was built with -DXMPP2_TEST_HOOKS=ON (ctest
# sets XMPP2_TEST_HOOKS=1|0; export it yourself when running by hand).
test_hooks_enabled() { [[ "${XMPP2_TEST_HOOKS:-0}" == 1 ]]; }

pass() { echo "PASS: $*"; }
fail() { echo "FAIL: $*" >&2; exit 1; }

# test_init NAME: common setup for a test script. Creates $WORK, installs the
# cleanup trap, builds the harness, creates the venv and (re)starts the server
# with the profile from XMPP2_SERVER_PROFILE (default full).
test_init() {
	WORK="$(mktemp -d "$SCRATCH/$1-XXXXXX")"
	HN=0
	trap test_cleanup EXIT
	ensure_venv
	ensure_harness
	start_server
	echo "== $1: prpl=$PRPL plugin_dir=${PLUGIN_DIR:-<system>} profile=${XMPP_PROFILE:-full}"
}

test_cleanup() {
	jobs -p | xargs -r kill 2>/dev/null || true
	cd "${SCRATCH:?}" && rm -rf "./$(basename "$WORK")"
	if [[ -z "${XMPP2_KEEP_SERVER:-}" ]]; then
		stop_server
	fi
}

# run_harness USER [HARNESS ARGS...] < script
# Runs the script from stdin; prints its output; fails the test on non-zero
# exit. The stdout file is left in $H_OUT, the debug/stderr file in $H_ERR.
run_harness() {
	local user="$1"
	shift
	HN=$((HN + 1))
	local script="$WORK/h-$HN.script"
	cat >"$script"
	H_OUT="$WORK/h-$HN.out"
	H_ERR="$WORK/h-$HN.err"
	if ! harness "$user" "$script" "$@" >"$H_OUT" 2>"$H_ERR"; then
		cat "$H_OUT"
		grep -v -E "amera|V4L2|GLib-CRITICAL|^$" "$H_ERR" | tail -20 >&2
		fail "harness run $HN ($user) failed"
	fi
	cat "$H_OUT"
}

# run_peer USER ARGS...: run a foreground peer; fail the test when it fails.
run_peer() {
	local out
	if ! out="$(peer "$@" 2>&1)"; then
		echo "$out" >&2
		fail "peer ($1) failed"
	fi
	echo "$out"
}
