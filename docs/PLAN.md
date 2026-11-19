# purple-xmpp2 v1 plan

Milestones in dependency order. Each lists the files it owns, so parallel
work does not collide. Design references point to `docs/ARCHITECTURE.md`.

Test vocabulary: "harness" is `tests/integration/xmpp2-harness` as alice,
"peer" is `tests/integration/peer.py` (bob, carol, or alice on another
resource such as `alice@localhost/phone`), server is `tests/server/run.sh`.
Every integration test also runs once with `PRPL=prpl-jabber` where that
makes sense, to document the before/after difference.

## Test contract (fixed by the tech lead)

| ctest label | Needs | Runs in |
|---|---|---|
| `unit` | nothing, no network | distro build containers (Debian 13, Ubuntu 24.04, Fedora) |
| `smoke` | pidgin + xvfb | CI host job |
| `integration` | podman Prosody | CI host job; exit 77 means skipped |

`tests/unit/` is owned by the engineer of each feature (unit tests ship in the
same change as the code). The rest of `tests/` is owned by QA. Tests assert
only on `xmpp2*` debug categories.

## Dependency graph

```
M0 scaffold --> M1 prune + hooks --+--> M2 carbons ------+
                                   +--> M3 MAM account --+--> M4 MAM rooms --+
                                   +--> M5 receipts+markers                  |
                                   +--> M6 correction                        +--> M10 SM resume --> M12 v1
                                   +--> M7 HTTP upload                       |
                                   +--> M8 migration                         |
                                   +--> M9 SM fixes ---------------------------+
M0 --> M11 packaging + CI (any time after M0)
```

## Parallel waves and file ownership

| Wave | Milestones in parallel | Shared files (conflict policy) |
|---|---|---|
| 1 | M0, then M1 (one owner, sequential) | everything |
| 2 | M2, M3, M5, M6, M7, M8, M9, M11 | `ext/ext.c` registry table and `CMakeLists.txt` source list: one line each, append only, rebase trivially. `namespaces.h` is pre-filled in M1; `x2stream.h` has no per-module fields (modules keep their state in the `priv[]` slot of their registry position via `x2_ext_get_priv()`), so no wave-2 milestone edits either |
| 3 | M4 (after M3), M10 (after M3 and M9, and after wave 2 has merged) | M10 owns `jabber.c`/`jabber.h` exclusively |

Wave-2 ownership:

| Milestone | Owns | Touches outside own files |
|---|---|---|
| M2 | `ext/carbons.c`, `core/carbon_parse.c` | none |
| M3 | `ext/mam.c`, `ext/statefile.c`, `core/mam_proto.c`, `core/store.c` | none |
| M5 | `ext/receipts.c`, `ext/markers.c`, `core/receipt_parse.c` | none |
| M6 | `ext/correction.c`, `core/replace_parse.c` | none |
| M7 | `ext/upload.c`, `core/upload_proto.c` | `libxmpp.c` (4 `prpl_info` pointers) |
| M8 | `ext/migrate.c` | `jabber.c` `jabber_actions()` (one entry) |
| M9 | `stream_management.c/.h` | none |
| M11 | `debian/`, `packaging/`, `.github/`, `README.md` install section | none |

## M0: scaffold builds and loads (in progress)

Scope: CMake build, `xmpp2_internal.h`, id/name/prefs/caps-file/cmd
renames, `-fvisibility=hidden`, smoke and baseline harness.

Acceptance:
- `cmake -B build && cmake --build build` succeeds with `-Wall -Wextra` on
  Debian 13 (libpurple 2.14.14) and Ubuntu 24.04 (2.14.12).
- `nm -D --defined-only build/libxmpp2.so` prints only `purple_init_plugin`.
- `tests/smoke/pidgin-load.sh` passes: Pidgin probes both `prpl-jabber`
  and `prpl-xmpp2`, both appear in the Add Account protocol list, and the
  debug log has no "cannot register existing type" or plugin load error.
- `PRPL=prpl-xmpp2 PLUGIN_DIR=build tests/integration/test_basic.sh` passes.
- A prpl-jabber account and an xmpp2 account (different JIDs) are connected
  at the same time in one Pidgin; both exchange messages.

## M1: prune and hook layer (single owner)

Scope (ARCHITECTURE 3, 5, 6):
- Delete `google/`, `jingle/`, `bosh.c`, `auth_cyrus.c`, `auth_digest_md5.c`
  and every branch using them; remove the BOSH URL account option and media
  prpl ops. Drop `XMPP2_CYRUS_SASL` from CMake.
- `src/xmpp2.map` version script, `--no-undefined`, CI symbol check script.
- `_()` gettext fallback to the `pidgin` domain; caps node and XEP-0092
  name.
- Rename the inherited debug category `jabber` to `xmpp2` mechanically
  (all files, one commit, no other change in it).
- `ext/ext.c/.h` registry, `ext/x2stream.h` with shared state and one
  `priv[]` slot per module (M1.1),
  `ext/msgctx.c` (X2Msg build and deliver), `message.c` delivery takes the
  context, `dir=OUT` path writes `SEND | REMOTE_SEND`.
- Login chain: own bare-JID `disco#info`, `pre_presence` and
  `session_ready` hooks.
- Outbound: `origin-id` UUID used as stanza id.
- All v1 namespaces in `namespaces.h`.
- `core/sid.c`, `core/forward.c`, `core/dedup.c` with GTest unit tests;
  `tests/unit/` wired into `ctest` with label `unit`; existing smoke and
  integration scripts registered under labels `smoke` and `integration`.
- Harness extensions needed by wave 2 (do them here, once):
  - `--userdir DIR` that persists across runs (state file and logs survive a
    restart); `--log txt` enables IM and chat logging in text format.
  - event lines `EV <kind> conv=<name> who=<jid> flags=<hex> mtime=<epoch> <text>`
    for every conversation write, including system and sent lines.
  - commands `join ROOM NICK`, `groupsend ROOM TEXT`, `correct JID TEXT`,
    `upload JID PATH`, `focus JID` (sets `unseen-count` 0 and emits
    `PURPLE_CONV_UPDATE_UNSEEN`), `logcount JID TEXT N`, `expect-ev ...`.
- Peer extensions: `--resource`, `raw XML` (send a hand-written stanza, for
  forged carbons and MAM results), `receipts on|off`, `markers on|off`,
  `upload PATH`.

Acceptance:
- All M0 acceptance still passes; `git grep -i 'google\|bosh\|cyrus\|jingle' src`
  only finds comments slated for removal (target: none).
- `ctest -L unit` passes in under 5 s without network; `ctest -L integration`
  exits 77 (skip) when podman is unavailable.
- No debug line uses the category `jabber`; all inherited lines log as
  `xmpp2`.
- Every outgoing `<message>` carries `<origin-id xmlns='urn:xmpp:sid:0' id=X>`
  with `X` equal to the stanza `id` (peer prints raw stanza).
- Libpurple debug log shows the order: roster result, `pre_presence`, initial
  presence, `session_ready`.
- Dedup unit tests: a message without origin-id or trusted stanza-id gets
  no key; the stanza `@id` is never a key (M1.1).
- `make`-time warning count does not increase versus M0.

## M1.1: review hardening (before wave 2)

Scope (ARCHITECTURE 1.2, 5, 6, 7.3): `server_info` only for trusted disco
sources (own domain, own server items, own bare JID; `account_info` folded
into it); `priv[]` slots replace per-module pointers in `x2stream.h`; no
`I:` dedup keys; `<forwarded><delay>` before the inner delay; `x2_msg_dup()`
and `x2_msg_redeliver()` define `X2_DEFERRED` ownership; live messages get
an archive, stanza-ids trusted only for archives that advertised sid/mam;
MUC PM detection from the muc#user hint; `message.c` takes delays from the
context only; SPDX headers on new files; `XMPP2_SANITIZE` CMake option.

Acceptance: `ctest -L unit` green on a normal and on an `XMPP2_SANITIZE=ON`
build, `test-msgctx` clean under valgrind.

## M2: carbons (XEP-0280)

Acceptance:
- Harness online, peer `alice/phone` sends "c1" to bob: harness shows
  `EV sent conv=bob@localhost flags` containing `REMOTE_SEND`, text "c1",
  exactly once, in the bob conversation, logged once.
- bob sends "c2" while harness and alice/phone are online: harness shows it
  once as incoming.
- Forged carbons, each sent by peer `raw`; for each: nothing displayed,
  `xmpp2-carbons` debug line says forged carbon dropped:
  1. outer `from='bob@localhost'` wrapping `<sent>`;
  2. outer `from` our bare JID (via a server-side test hook or a unit test
     on `core/carbon_parse.c` where a peer cannot forge the outer `from`),
     `<sent>` whose inner `from` is `bob@localhost`;
  3. same, `<received>` whose inner `to` is `carol@localhost`.
  Cases 2 and 3 are mandatory as `unit` tests and as integration tests
  where the server allows.
- Server without carbons (disable `mod_carbons` via a test config switch):
  login succeeds, no enable IQ sent.

## M3: MAM catch-up for the account archive (XEP-0313)

Acceptance (all with persistent `--userdir`):
1. First run of a fresh account (default `mam_initial_days=1`): messages bob
   sent in the last day are shown, older ones are not. First run of a
   cloned account (`initial=cursor-only`): nothing shown; state file has
   `cursor`. State file path is
   `<userdir>/xmpp2/<escaped full username>.state`, directory mode 0700.
2. Harness offline; bob sends m1..m3 with 2 s gaps. Harness connects: three
   incoming events in bob's conversation, in order, flags include
   `DELAYED`, `mtime` within 2 s of the send times, each logged once.
3. Restart with no new traffic: zero history events.
4. Harness offline; `alice/phone` sends "p1" to carol: on connect, outgoing
   `REMOTE_SEND` "p1" in carol's conversation.
5. Harness online; alice/phone sends "p2" (sent carbon, no stanza-id on
   Prosody). Restart harness: "p2" is not shown again (ring dedup).
6. Harness sends "h1" to bob, quits, restarts: "h1" not shown again.
7. Offline storage and MAM overlap (Prosody `mod_offline` on): offline
   message shown exactly once.
8. 250 messages while offline (page size 100): all 250 shown, in order,
   three pages in the debug log.
9. Live during catch-up: peer sends "live1" right after harness signs on
   while sync of 250 messages runs: "live1" shown once, after the 250.
10. Corrupt the `cursor` in the state file: catch-up falls back to
    `start=cursor_ts`, no error dialog, no duplicates of pre-cursor messages.
11. Forged MAM results, nothing displayed for each:
    a. `raw` from bob with `<result>` and a random queryid;
    b. (unit test on the inner check, since a peer cannot forge the outer
       `from`) account-archive result whose inner `from` and `to` are both
       foreign JIDs.
12. Server without MAM: login works, behaviour equals M1.
13. Ordering: debug log shows the carbons enable IQ result before the first
    MAM query IQ.
14. Watchdog: delay MAM responses (test hook or a server module that stalls
    the query) for 35 s: after about 30 s the archive goes `LIVE`, queued
    live messages appear, one "history incomplete" notice is shown.
15. Teardown during sync: kill the connection while live messages are
    queued: those messages are shown before the account goes offline.
16. Page cap: 2500 messages while offline: at most 20 pages fetched, the
    newest page shown, one "older history skipped" notice per affected
    conversation.
17. item-not-found retry with `start=cursor_ts`: the message whose id equals
    the old cursor is not shown again.
- Unit: `mam_proto` query building (with and without `after`/`start`),
  `<fin>` parsing incl. missing `<last>`, `store` round-trip, atomic write,
  corrupt file yields empty state plus a warning.

## M4: MAM for rooms

Acceptance:
1. carol posts r1..r5 in `test@conference.localhost` while alice is not in
   the room; harness joins: five messages, in order, `DELAYED`, once.
2. Leave and rejoin with no new traffic: zero history messages (with
   prpl-jabber the same test shows legacy history again: record it).
3. Cursor older than `mam_muc_threshold_days`: only the last 50 messages
   plus one "older history skipped" system line.
4. Room with MAM disabled: legacy history behaviour unchanged.
5. Own message sent from alice/phone into the room while harness was away:
   shown once, attributed to alice's nick.
6. MUC private message via MAM opens a conversation with the occupant full
   JID.
7. Join presence carries `<history maxchars='0' maxstanzas='0'/>` for MAM
   rooms (peer or server log).
8. Nick change (`/nick`) after joining: no new room MAM query.
9. Cursor fresh but more than 10 pages behind: last page plus one "older
   history skipped" line.
10. Spoofed room result: a room-archive result whose inner `from` bare JID
    is not the room (unit test on the inner check) is dropped; a room result
    with inner `type='chat'` is still delivered into the room, never as a
    1:1 conversation.

## M5: receipts and chat markers (XEP-0184, XEP-0333)

Acceptance:
- Outgoing 1:1 messages carry `<request xmlns='urn:xmpp:receipts'/>` and
  `<markable xmlns='urn:xmpp:chat-markers:0'/>`; groupchat ones carry
  neither.
- bob (roster contact) sends with a receipt request: peer receives
  `<received id=...>` for it. A non-roster sender gets none. A MAM-fetched
  or carbon-copied message triggers none.
- bob sends a markable message, harness `focus bob@localhost`: peer receives
  exactly one `<displayed id=...>` for the newest message; a second focus
  without new messages sends nothing.
- bob acknowledges a harness message: signal `xmpp2-receipt` fires (debug
  line); with the notices option on, one system line, not logged.
- Caps advertise both namespaces (peer `disco`).

## M6: message correction (XEP-0308)

Acceptance:
- bob sends "teh", then a correction "the" referencing it: harness shows
  "the" with the "corrected:" marker.
- bob sends a correction referencing a message id from carol: shown as a
  normal message without the marker.
- Harness `correct bob@localhost fixed`: peer receives a message with
  `<replace id=...>` equal to the id of the harness's previous message.
- Caps advertise `urn:xmpp:message-correct:0`.

## M7: HTTP File Upload (XEP-0363)

Acceptance:
- `upload bob@localhost /path/file.png` (1 MiB): xfer completes; peer
  receives a body that is an `http(s)://` URL with matching `x:oob`; a GET
  of that URL returns identical bytes (sha256).
- Upload to a room: room members receive the URL.
- File above `max-file-size`: xfer fails before any network request with a
  clear error.
- Server without upload component: falls back to SI (upstream behaviour).
- Unit: PUT request building; header values containing CR/LF are rejected;
  non-allowed slot headers are dropped.

## M8: migration from prpl-jabber

Acceptance (Pidgin under Xvfb with a prepared `accounts.xml` and `blist.xml`,
or the harness calling `x2_migrate_run()` through a test command):
- In Pidgin, with one prpl-jabber account and no xmpp2 account, the offer
  appears once; "Copy" creates an xmpp2 account with the same username,
  password, alias, resource, connection security and port; the original
  remains, disabled.
- In the harness (UI id not `pidgin`) no dialog appears at startup; the prpl
  action still performs the copy.
- An originally disabled prpl-jabber account yields a disabled clone and
  stays disabled.
- Room bookmarks (blist chats) of the original exist for the clone.
- Running the migration again creates nothing new.
- "Never" persists across restarts; "Not now" offers again next start.
- The clone's first connect displays no history (`initial=cursor-only`) and
  its logs land in the same `logs/jabber/<user>/` directory.

## M9: stream management fixes

Acceptance:
- Peer `streamfeatures`-style check plus debug log: `<enable resume='true'/>`
  sent, `<enabled id max>` parsed and logged.
- Sending 20 messages in a burst produces at most 5 `<r/>` (was 20).
- Unit-style test of `h` arithmetic across 2^32 wrap.
- Kill the TCP connection (`ss -K` or podman network disconnect) while one
  message is unacked; after Pidgin's reconnect the message reaches bob
  exactly once (with MAM), and no stale IQ or presence is resent.
- `<failed h='N'>` trims exactly N stanzas (debug log).

## M10: silent reconnect with resumption

Acceptance:
- Drop the TCP connection for 5 s while connected: the account never shows
  offline in the UI, buddies keep their presence, joined rooms stay joined,
  debug log shows `<resumed>`; messages bob sent during the gap appear once.
- Drop longer than the server's `max`: falls back to a normal reconnect and
  MAM catch-up; nothing lost or duplicated.
- Messages typed during the gap are delivered once after resume.
- All earlier integration tests still pass.

## M11: packaging and CI

Acceptance:
- `dpkg-buildpackage -us -uc` on Debian 13 and Ubuntu 24.04 produces a
  package installing `libxmpp2.so` into the purple plugin dir; `lintian`
  has no errors.
- `rpmbuild -ba packaging/rpm/purple-xmpp2.spec` on current Fedora succeeds.
- GitHub Actions: build plus unit tests on the three distros, symbol check,
  integration suite on Debian 13 with the Prosody container, release-please
  with `include-commit-authors`.

## M12: v1 release

Acceptance:
- All milestone acceptance tests green in CI.
- Manual test pass in real Pidgin against Prosody and one ejabberd instance
  (ejabberd stamps stanza-ids differently; verify M3 tests 4 and 5).
- README: install, coexistence with prpl-jabber, migration, known limits
  (no visual ticks, no OMEMO, no A/V, no BOSH).

## Tech lead decisions (2026-11-19)

- MAM (XEP-0313) is the offline-messaging mechanism. Stream management v1
  means fixing upstream behaviour: batched acks, never resend the unacked
  queue blindly on a fresh session. Resumption is a best-effort silent
  reconnect (M10, last milestone, may slip to v0.2).
- Drop `google/`, `jingle/`, BOSH, Cyrus SASL and DIGEST-MD5 in M1.
- Receipts and chat markers: protocol, signals and an optional notice line in
  v1. No GTK tick UI in v1.
- MAM first sync: fresh accounts fetch 1 day (`mam_initial_days`, 0 means
  cursor-only). Cloned accounts use cursor-only (no replay of history
  prpl-jabber already logged).
- Migration: automatic offer only when the UI is Pidgin
  (`purple_core_get_ui()`); the prpl action is always available. A clone of a
  disabled account stays disabled.
- Reviewer findings accepted: archive and carbon spoofing checks, MAM query
  after the carbons enable result, SYNCING watchdog and flush on teardown,
  stricter dedup keys, state file keyed by full username in a 0700 dir,
  inclusive-start skip on item-not-found, MUC `maxstanzas='0'` and once-per-join
  query, page cap with notice for the account archive.
- Inherited debug category `jabber` becomes `xmpp2` in M1.
- Target libpurple >= 2.14.12 (Ubuntu 24.04).
- No release PR is merged until every v1 milestone has passed QA; the tech
  lead gates the merge.
