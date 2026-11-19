# Tests

Three layers, cheapest first. Requirements: podman (rootless), gcc,
libpurple-dev, python3 with venv, xvfb-run and pidgin (smoke only).

## 1. Test server (`tests/server/`)

Prosody 13 (Debian 13 package, built from `Containerfile`) on loopback with
mod_mam, mod_carbons, mod_smacks, mod_csi, MUC with muc_mam and
http_file_share (XEP-0363). Self-signed TLS, plaintext auth allowed.

    tests/server/run.sh start     # build image, run, register users
    tests/server/run.sh stop|reset|status|logs|cert

Ports (loopback only): c2s `15222`, HTTP `15280`. Domain `localhost`, MUC
`conference.localhost`, upload `upload.localhost`. Users `alice`, `bob`,
`carol`, password `secret`. Override with `XMPP2_TEST_C2S_PORT`,
`XMPP2_TEST_HTTP_PORT`, `XMPP2_TEST_PASSWORD`.

## 2. Integration (`tests/integration/`)

- `harness.c` builds to `xmpp2-harness`, a headless libpurple client driven
  by a line script (`connect`, `send`, `wait`, `absent`, `count`, `sleep`,
  `disconnect`, `quit`; see the header of the file). Exit 0 or 1.
- `peer.py` is a scripted slixmpp peer (`send`, `wait`, `mam`, `disco`,
  `join`, `groupsend`, `groupwait`, `streamfeatures`). It can send while the
  harness is offline, which is the basis for MAM catch-up tests.
- `lib.sh` has shared helpers (venv, harness build, server, `peer`,
  `harness`). Scratch lives in `$XMPP2_SCRATCH` (default
  `$TMPDIR/xmpp2-scratch-<uid>`); slixmpp is installed into a venv there.
- `test_basic.sh` is the 1:1 baseline, both directions.

Baseline with the built-in Pidgin jabber prpl, then the plugin under test:

    tests/integration/test_basic.sh
    PRPL=prpl-xmpp2 PLUGIN_DIR=build tests/integration/test_basic.sh

`XMPP2_KEEP_SERVER=1` leaves the container running after a test. Add
`--debug` to a `harness` call to see libpurple debug on stderr.

## 3. Smoke (`tests/smoke/pidgin-load.sh`)

Runs Pidgin under Xvfb with an isolated config dir (and redirected `HOME`),
checks the plugin is probed and loads, and that `prpl-xmpp2` registers.
Exit 77 means skipped (plugin not built).

    tests/smoke/pidgin-load.sh [build/libxmpp2.so]
