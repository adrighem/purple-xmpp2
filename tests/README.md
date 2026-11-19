# Tests

Three layers, cheapest first. Requirements: podman (rootless), gcc,
libpurple-dev, python3 with venv, xvfb-run and pidgin (smoke only).

## 1. Test server (`tests/server/`)

Prosody 13 (Debian 13 package, built from `Containerfile`) on loopback with
mod_mam, mod_carbons, mod_smacks, mod_csi, MUC with muc_mam and
http_file_share (XEP-0363). Self-signed TLS, plaintext auth allowed.

    tests/server/run.sh start     # build image, run, register users
    tests/server/run.sh stop|reset|status|logs|cert

Profiles: `XMPP2_SERVER_PROFILE=full|nocarbons|nomam|noupload` (or a custom
comma list such as `carbons,mam`) switches features off; changing profile
restarts the container. mod_offline is loaded, `max_archive_query_results`
is 100 (matches the PLAN page size).

Ports (loopback only): c2s `15222`, HTTP `15280`. Domain `localhost`, MUC
`conference.localhost`, upload `upload.localhost`. Users `alice`, `bob`,
`carol`, password `secret`. Override with `XMPP2_TEST_C2S_PORT`,
`XMPP2_TEST_HTTP_PORT`, `XMPP2_TEST_PASSWORD`.

## 2. Integration (`tests/integration/`)

- `harness.c` builds to `xmpp2-harness`, a headless libpurple client driven
  by a line script. The full command and filter reference is the header
  comment of the file. Highlights: `--userdir` (persistent state, logs),
  `--log txt`, machine-readable `EV` lines for every conversation write,
  `expect-ev|expect-next|no-ev|count-ev` with filters (`kind conv who ctype
  flag noflag mtime timeout n text`), `expect-debug|no-debug|count-debug`
  (xmpp2* categories only), `join|leave|groupsend`, `buddy-add`, `focus`,
  `cmd`, `correct`, `upload|upload-fail`, `logcount`, `pref`, `drop`
  (shutdown() the c2s socket without a stream close, like a network drop).
  Exit 0 or 1.
- `peer.py` is a scripted slixmpp peer (reference in its docstring):
  `--resource`, `send|sendr|sendmany|correct|raw`, `wait|nowait`,
  `expect-stanza|no-stanza|count-stanza|expect-out` (regex on raw stanzas),
  `receipts|markers|carbons on|off`, `mam`, `disco`, `join`, `upload`,
  `sha256`, `befriend`, `abort`, `--stay SECS`. It can send while the harness
  is offline, which is the basis for MAM catch-up tests.
- `lib.sh` has shared helpers. A feature test is just:

      source "$HERE/lib.sh"; test_init carbons
      peer_bg "$WORK/phone.log" alice --stay 30 --do "send bob@localhost c1"
      run_harness alice <<SCRIPT
      connect 30
      expect-ev kind=sent flag=REMOTE_SEND text=c1
      SCRIPT

  `test_init` sets up `$WORK`, cleanup and the server (profile from
  `XMPP2_SERVER_PROFILE`); `HUSERDIR`/`HLOG` give a persistent userdir and
  logs; `PEER_RESOURCE=phone` picks the peer resource. Scratch lives in
  `$XMPP2_SCRATCH`; slixmpp is pinned in `requirements.txt`.
  Put `timeout=` and other filter keys before `text=`, which takes the rest
  of the line.
- `test_basic.sh` is the 1:1 baseline; `test_selftest.sh` exercises every
  harness and peer primitive (works with both prpls). `test_sm.sh` covers
  stream management (prpl-xmpp2 only, else exit 77).

Running several suites at once on one host: use a private server with
`XMPP2_TEST_CONTAINER=name XMPP2_TEST_C2S_PORT=N XMPP2_TEST_HTTP_PORT=M`,
otherwise they restart each other's container.

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

## CMake / ctest

`tests/CMakeLists.txt` (included via `if(BUILD_TESTING) add_subdirectory(tests)`)
builds `xmpp2-harness` and registers tests by label: `unit` (export check,
plus `tests/unit/` if present), `smoke`, `integration` (needs podman, serial).
Missing tools give exit 77 (skipped).

    ctest --test-dir build -L unit
    ctest --test-dir build -L smoke
    ctest --test-dir build -L integration
