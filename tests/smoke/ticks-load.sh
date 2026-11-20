#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 purple-xmpp2 contributors
# Smoke test: Pidgin (under Xvfb) loads pidgin-xmpp2-ticks.so as a saved,
# enabled plugin, once next to libxmpp2.so (it connects to the prpl
# signals) and once without it (it waits, quietly). Never touches ~/.purple.
#   tests/smoke/ticks-load.sh path/to/pidgin-xmpp2-ticks.so path/to/libxmpp2.so
# Exit: 0 pass, 1 fail, 77 skipped (not built, or pidgin/xvfb-run missing).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
TICKS="${1:?usage: ticks-load.sh pidgin-xmpp2-ticks.so libxmpp2.so}"
PRPL_SO="${2:?usage: ticks-load.sh pidgin-xmpp2-ticks.so libxmpp2.so}"
RUN_SECS="${RUN_SECS:-6}"

for so in "$TICKS" "$PRPL_SO"; do
	[[ -f "$so" ]] || { echo "SKIP: $so not built"; exit 77; }
done
for tool in pidgin xvfb-run; do
	command -v "$tool" >/dev/null || { echo "SKIP: $tool missing"; exit 77; }
done

WORK="$(mktemp -d "${TMPDIR:-/tmp}/xmpp2-ticks-smoke-XXXXXX")"
cleanup() {
	jobs -p | xargs -r kill 2>/dev/null || true
	cd "${TMPDIR:-/tmp}" && rm -rf "./$(basename "$WORK")"
}
trap cleanup EXIT

# HOME and XDG dirs are redirected so nothing leaks into the real profile.
export HOME="$WORK/home"
export XDG_CONFIG_HOME="$WORK/home/.config"
export XDG_DATA_HOME="$WORK/home/.local/share"
export XDG_CACHE_HOME="$WORK/home/.cache"
mkdir -p "$HOME"

# shellcheck source=../integration/lib.sh
source "$ROOT/tests/integration/lib.sh"

fail() {
	echo "FAIL: $*" >&2
	grep -i -E "ticks|xmpp2|g_log" "$LOG" | head -30 >&2 || true
	exit 1
}

# run_pidgin NAME [SO...]: fresh config with the given plugins installed and
# the ticks plugin saved as loaded; the debug log is left in $LOG.
run_pidgin() {
	local name="$1" conf
	shift
	conf="$WORK/$name"
	mkdir -p "$conf/plugins"
	cp "$@" "$conf/plugins/"
	cat >"$conf/prefs.xml" <<-PREFS
	<?xml version='1.0' encoding='UTF-8' ?>
	<pref version='1' name='/'>
		<pref name='pidgin'>
			<pref name='plugins'>
				<pref name='loaded' type='pathlist'>
					<item value='$conf/plugins/$(basename "$TICKS")'/>
				</pref>
			</pref>
		</pref>
	</pref>
	PREFS
	LOG="$WORK/$name.log"
	pidgin_xvfb "$RUN_SECS" "$conf" "$LOG" || fail "Pidgin did not exit ($name)"
}

check_clean() {
	if grep -E "pidgin-xmpp2-ticks.*(not usable|could not be loaded|unresolved|undefined symbol)" "$LOG"; then
		fail "$1: plugin failed to load"
	fi
	grep -q "xmpp2-ticks: loaded" "$LOG" || fail "$1: plugin was not loaded"
	# GLib criticals reach Pidgin's debug log as "g_log:"; other installed
	# plugins may log theirs, so only ours and signal errors count.
	if grep -E "signals: (Something tried|Signal data for xmpp2)|g_log: .*(tick|xmpp2|imhtml|GtkTextBuffer|gtk_text)" "$LOG"; then
		fail "$1: GLib-CRITICAL or signal error"
	fi
}

run_pidgin with-prpl "$TICKS" "$PRPL_SO"
check_clean with-prpl
grep -q "xmpp2-ticks: connected to prpl-xmpp2 signals" "$LOG" ||
	fail "with-prpl: did not connect to the prpl signals"
echo "OK: loaded next to prpl-xmpp2 and connected to its signals"

run_pidgin without-prpl "$TICKS"
check_clean without-prpl
if grep -q "xmpp2-ticks: connected to prpl-xmpp2 signals" "$LOG"; then
	echo "NOTE: a system-wide prpl-xmpp2 was found; missing-prpl case not covered"
else
	grep -q "xmpp2-ticks: prpl-xmpp2 not loaded yet" "$LOG" ||
		fail "without-prpl: missing prpl not noticed"
	echo "OK: loaded without prpl-xmpp2, waiting for it"
fi
echo "PASS: ticks-load"
