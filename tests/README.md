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
`XMPP2_TEST_HTTP_PORT`, `XMPP2_TEST_PASSWORD`. Upload tests also use
`XMPP2_UPLOAD_LIMIT=BYTES` (max-file-size, default 10 MiB) and
`XMPP2_UPLOAD_HTTPS=1` (upload URLs are https on `XMPP2_TEST_HTTPS_PORT`,
default HTTP port + 1).

## 2. Integration (`tests/integration/`)

- `harness*.c` builds to `xmpp2-harness`, a headless libpurple client driven
  by a line script. `xmpp2-harness --help` lists every command from the
  dispatch table (`commands[]` in `harness_script.c`, which also holds the
  full command and filter reference). Modules: `harness_main.c` (args, core
  init, loop), `harness_core.c` (run state, completion, polling),
  `harness_ui.c` (conversation/notify/request/debug UI ops, `EV` lines),
  `harness_filter.c` (flags and filters), `harness_cmds_{session,msg,rooms,
  files,assert}.c` (handlers). Highlights: `--userdir` (persistent state, logs),
  `--log txt`, machine-readable `EV` lines for every conversation write,
  `expect-ev|expect-next|no-ev|count-ev` with filters (`kind conv who ctype
  flag noflag mtime timeout n text`; `kind=notify` matches
  `purple_notify_message()` dialogs), `expect-debug|no-debug|count-debug`
  (xmpp2* categories only), `join|leave|groupsend`, `buddy-add`, `focus`,
  `cmd`, `correct`, `upload|upload-fail|upload-drop`, `logcount`, `pref`,
  `drop` (shutdown() the c2s socket without a stream close, like a network
  drop), `raw|raw-refused` (send XML through the prpl's `send_raw`, the XML
  console path),
  `action TEXT` (run a prpl action by label), `plugin-action ID TEXT` (run the
  action of a standard plugin, as Tools > Plugin Actions does), `answer N`/`no-request`
  (`purple_request_action()` has a recording UI: the script presses a
  button), `remove-account` (delete the account as the Accounts dialog
  does, then stop), `expect-account PRPL USER enabled= connected= exists=` (any account
  of the userdir) and `--libpref s:|i:|b:/path=V` (libpurple pref before the
  main loop). Conversation events (`EV`) come from every account of the
  userdir, and the buddy list has an empty UI so `blist.xml` is written.
  Exit 0 or 1.
- `peer.py` is a scripted slixmpp peer (reference in its docstring):
  `--resource`, `--feature NS` (advertise a caps feature), `send|sendr|sendmany|correct|raw`,
  `groupsend|groupsendmany` (rooms), `wait|nowait`,
  `expect-stanza|no-stanza|count-stanza|expect-out` (regex on raw stanzas),
  `receipts|markers|carbons on|off`, `bind NAME REGEX` (store group 1 of a stanza, `${NAME}` in later actions), `mam`, `disco`, `join`, `upload`,
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
  `$XMPP2_SCRATCH` (default `${TMPDIR:-/tmp}/xmpp2-scratch-<uid>-<container>`,
  one per `XMPP2_TEST_CONTAINER`); slixmpp is pinned in `requirements.txt`.
  Put `timeout=` and other filter keys before `text=`, which takes the rest
  of the line.
- `test_upload.sh` (XEP-0363: contact, room, size limit, http opt-in) and
  `test_upload_fallback.sh` (profile `noupload`, SI fallback). The test
  server serves plain http, so they set `--pref b:allow_plain_http_upload=1`.
  The streaming upload has three more: `test_upload_stream.sh` (50 MiB sparse
  file, byte for byte, progress, peak RSS via `rss-reset`/`rss-check`),
  `test_upload_errors.sh` (500, redirect, early 413, backpressure, cancel,
  sign-off and account removal mid-upload, through `upload_stub.py`, a CONNECT
  proxy that fakes the upload server; the harness takes `--proxy HOST:PORT`)
  and `test_upload_https.sh` (Prosody with https upload URLs). The harness
  prints `EV xfer-progress` lines and `xfer-cancel-at BYTES [cancel|
  disconnect|remove]` acts mid-transfer (`tests/integration/xfer_ui.h`).
- `test_correct.sh` covers XEP-0308 (incoming marker, forged ids, rooms,
  `/correct`, caps, offline delivery).
- `test_migrate.sh` (M8): seeds prpl-jabber accounts and a buddy list in the
  userdir, copies them with the prpl action or the startup offer and checks
  `accounts.xml`, `blist.xml`, `prefs.xml`, state files and logs with
  python; a real Pidgin under xvfb-run checks the offer when installed.
- `test_basic.sh` is the 1:1 baseline; `test_selftest.sh` exercises every
  harness and peer primitive (works with both prpls). `test_sm.sh` covers
  stream management (prpl-xmpp2 only, else exit 77). `test_mam_muc.sh`,
  `test_mam_muc_bulk.sh` and `test_mam_muc_legacy.sh` (profile `nomam`)
  cover room history (PLAN M4); the state file's `[archive <room>]`
  section holds the room cursor.
- `test_ticks.sh` (PLAN M13) runs a real Pidgin under xvfb-run with
  `pidgin-xmpp2-ticks.so` (`TICKS_SO`) and an alice account in a scratch
  profile; the plugin's test hook (`XMPP2_TICKS_TEST_PEER`, needs
  `XMPP2_TEST_HOOKS`) sends one line to bob, who answers with a receipt and a
  displayed marker, and logs the view's line after each update: one tick,
  then two, then clear and close without criticals.

### Adding a harness command

A command is one handler plus one table row:

1. Write `void cmd_foo(const char *cmd, char *arg)` in the fitting
   `harness_cmds_*.c` (or a new file listed in `tests/CMakeLists.txt`; `lib.sh`
   builds every `harness*.c`). `cmd` is the name as typed, `arg` the rest of
   the line (use `split_word()` to peel off words).
2. Finish exactly once: `continue_ok(cmd, detail)` or `fail_now(cmd, detail)`
   for immediate results, or `arm(P_..., cmd, secs)` / `poll_arm()` to wait
   and let `resume()` report (the first FAIL ends the run).
3. Declare it in `harness.h` and add `{ "foo", cmd_foo, "ARGS  what it does" }`
   to `commands[]` in `harness_script.c`; `--help` picks the help text up.
4. Add a step to `test_selftest.sh` and a line to the highlights above.

Running several suites at once on one host: use a private server with
`XMPP2_TEST_CONTAINER=name XMPP2_TEST_C2S_PORT=N XMPP2_TEST_HTTP_PORT=M`,
otherwise they restart each other's container. The default scratch dir
includes the container name, so their `server.env` files stay apart.

Baseline with the built-in Pidgin jabber prpl, then the plugin under test:

    tests/integration/test_basic.sh
    PRPL=prpl-xmpp2 PLUGIN_DIR=build tests/integration/test_basic.sh

`XMPP2_KEEP_SERVER=1` leaves the container running after a test. Add
`--debug` to a `harness` call to see libpurple debug on stderr.

## 3. Smoke (`tests/smoke/pidgin-load.sh`)

Runs Pidgin under Xvfb with an isolated config dir (and redirected `HOME`),
checks the plugin is probed and loads, and that `prpl-xmpp2` registers.
It fails on a GLib-CRITICAL from our code during startup (a signal
connected before libpurple registers it, or a failed `g_return_*` check of
an `x2_`/`jabber_` function).
Exit 77 means skipped (plugin not built).

    tests/smoke/pidgin-load.sh [build/libxmpp2.so]

`tests/smoke/ticks-load.sh` does the same for the ticks plugin (saved as
loaded in `prefs.xml`), once with `libxmpp2.so` (it must connect to the prpl
signals) and once without (it must wait quietly); built only when the ticks
plugin is.

    tests/smoke/ticks-load.sh build/pidgin-plugin/pidgin-xmpp2-ticks.so build/libxmpp2.so

## CMake / ctest

`tests/CMakeLists.txt` (included via `if(BUILD_TESTING) add_subdirectory(tests)`)
builds `xmpp2-harness` and registers tests by label: `unit` (export check,
plus `tests/unit/` if present), `smoke`, `integration` (needs podman, serial).
Missing tools give exit 77 (skipped).

    ctest --test-dir build -L unit
    ctest --test-dir build -L smoke
    ctest --test-dir build -L integration

Test-only hooks in the plugin (`XMPP2_TEST_MAM_STALL` and the like) exist
only with `-DXMPP2_TEST_HOOKS=ON` (CI sets it). ctest passes
`XMPP2_TEST_HOOKS=1|0` to every test; a test that needs the hooks exits 77
(or skips the step) when it is 0. Run by hand with `XMPP2_TEST_HOOKS=1` on a
hooks build.
