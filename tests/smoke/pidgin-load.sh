#!/usr/bin/env bash
# Smoke test: Pidgin (under Xvfb) loads the plugin from an isolated config
# directory, and the protocol id registers. Never touches ~/.purple.
#   tests/smoke/pidgin-load.sh [path/to/libxmpp2.so]
# Exit: 0 pass, 1 fail, 77 skipped (plugin not built; ctest SKIP_RETURN_CODE).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SO="${1:-${PLUGIN_SO:-$ROOT/build/libxmpp2.so}}"
PRPL="${PRPL:-prpl-xmpp2}"
RUN_SECS="${RUN_SECS:-6}"

if [[ ! -f "$SO" ]]; then
	echo "SKIP: $SO not built"
	exit 77
fi
for tool in pidgin xvfb-run; do
	command -v "$tool" >/dev/null || { echo "SKIP: $tool missing"; exit 77; }
done

WORK="$(mktemp -d "${TMPDIR:-/tmp}/xmpp2-smoke-XXXXXX")"
cleanup() {
	jobs -p | xargs -r kill 2>/dev/null || true
	cd "${TMPDIR:-/tmp}" && rm -rf "./$(basename "$WORK")"
}
trap cleanup EXIT

mkdir -p "$WORK/home" "$WORK/conf/plugins"
cp "$SO" "$WORK/conf/plugins/"
SO_NAME="$(basename "$SO")"
LOG="$WORK/pidgin.log"

# HOME and XDG dirs are redirected so nothing leaks into the real profile.
export HOME="$WORK/home"
export XDG_CONFIG_HOME="$WORK/home/.config"
export XDG_DATA_HOME="$WORK/home/.local/share"
export XDG_CACHE_HOME="$WORK/home/.cache"

# shellcheck source=../integration/lib.sh
source "$ROOT/tests/integration/lib.sh"
pidgin_xvfb "$RUN_SECS" "$WORK/conf" "$LOG" || {
	echo "FAIL: Pidgin did not exit" >&2
	exit 1
}

fail() {
	echo "FAIL: $*" >&2
	grep -i -E "$SO_NAME|xmpp2" "$LOG" | head -20 >&2 || true
	exit 1
}

grep -q "probing .*/$SO_NAME" "$LOG" || fail "pidgin never probed $SO_NAME"
if grep -E "$SO_NAME.*(not usable|could not be loaded|unresolved|undefined symbol)" "$LOG"; then
	fail "$SO_NAME failed to load"
fi
if grep -q "cannot register existing type" "$LOG"; then
	fail "GType registration clash with built-in prpl-jabber"
fi
# A signal connected before libpurple registers it (prpls load before
# accounts_init() and friends) logs "signal_connect_common: should not be
# reached" on every start. Pidgin routes GLib criticals to its debug log as
# "g_log:". Fail on those and on criticals of our own functions (other
# installed plugins may log theirs).
if grep -E "signals: Something tried|g_log: .*(signal_connect_common|x2_|jabber_|xmpp2)" "$LOG"; then
	fail "GLib-CRITICAL from xmpp2 during startup"
fi
echo "OK: pidgin probed and loaded $SO_NAME "

# Confirm both protocol ids register side by side, using the headless harness.
if command -v gcc >/dev/null && ensure_harness 2>/dev/null; then
	: >"$WORK/empty.script"
	for id in "$PRPL" prpl-jabber; do
		if ! "$HARNESS" --prpl "$id" --plugin-dir "$(dirname "$SO")" --debug \
			--user "smoke@localhost" --password x --script "$WORK/empty.script" \
			>"$WORK/h.out" 2>"$WORK/h.err"; then
			grep "protocol" "$WORK/h.err" >&2 || true
			fail "protocol $id not registered"
		fi
		if grep -q "cannot register existing type" "$WORK/h.err"; then
			fail "GType clash when loading with $id"
		fi
		echo "OK: protocol $id registered with libpurple"
	done
fi
echo "PASS: pidgin-load"
