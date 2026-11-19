#!/usr/bin/env bash
# Run a test command only when podman is available, else exit 77 (skip).
set -euo pipefail
command -v podman >/dev/null || { echo "SKIP: podman missing"; exit 77; }
exec "$@"
