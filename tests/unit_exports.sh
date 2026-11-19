#!/usr/bin/env bash
# The plugin must export exactly one symbol: purple_init_plugin.
#   tests/unit_exports.sh path/to/libxmpp2.so
set -euo pipefail

SO="${1:?usage: unit_exports.sh libxmpp2.so}"
if [[ ! -f "$SO" ]]; then
	echo "SKIP: $SO not built"
	exit 77
fi
got="$(nm -D --defined-only "$SO" | awk '$2 ~ /^[TDBRVW]$/ {print $3}' | sort)"
if [[ "$got" == "purple_init_plugin" ]]; then
	echo "PASS: only purple_init_plugin exported"
else
	echo "FAIL: unexpected exports:" >&2
	echo "$got" >&2
	exit 1
fi
