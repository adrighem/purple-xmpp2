# purple-xmpp2 architecture

A libpurple 2.14 protocol plugin (`prpl-xmpp2`, `libxmpp2.so`) forked from
Pidgin 2.14.14's built-in jabber prpl, modernised for multi-device XMPP:
history catch-up (MAM), carbons, stream management, HTTP upload, receipts,
chat markers and message correction. It coexists with the built-in
`prpl-jabber` in the same Pidgin process.

Design priorities, in order: correctness of message delivery (never lose,
never duplicate), small diff against upstream for inherited code, new code in
small single-purpose files with the protocol logic unit-testable without a
running libpurple core.

## 1. Coexistence with the built-in prpl-jabber

Both plugins can be loaded at once. Everything that is process-global must be
distinct. Checked against the 2.14.14 sources:

| Namespace | Upstream value | Ours | Status / note |
|---|---|---|---|
| ELF symbols | `jabber_*` exported from `libjabber.so.0` | all hidden except `purple_init_plugin` | see 1.1 |
| Plugin id | `prpl-jabber` | `prpl-xmpp2` (`XMPP2_PRPL_ID`) | done in M0 |
| Plugin name (account dropdown) | `XMPP` | `XMPP (xmpp2)` | done in M0 |
| Slash commands `prpl_id` | `prpl-jabber` | `XMPP2_PRPL_ID` | done in M0 |
| Prefs root | `/plugins/prpl/jabber` | `/plugins/prpl/xmpp2` | done in M0 |
| Caps cache file in `purple_user_dir()` | `xmpp-caps.xml` | `xmpp2-caps.xml` | done in M0 |
| XEP-0115 caps node | `http://pidgin.im/` | `XMPP2_WEBSITE` | M1 (ver hash differs anyway; node should name the software) |
| XEP-0092 software version | `libpurple` | `purple-xmpp2 <version>` | M1 |
| GObject type names | `JingleSession`, `JingleContent`, `JingleRtp`, ... | none | Jingle dropped, see 3. `g_type_register_static` is process-global: a second registration of the same name fails and the type getter returns 0 |
| Signals, IPC (`jabber-receiving-xmlnode`, ...) | registered on the plugin instance | same names on our instance | no clash: purple signals are keyed by instance. Third-party plugins (carbons, lurch) look up `prpl-jabber` and will not attach to us, which is what we want. Semantics differ, see 1.2 |
| `uri-handler` for `xmpp:` | both connect | first connected account wins | acceptable |
| gettext domain | `pidgin` | `purple-xmpp2`, falling back to `pidgin` | see 2 |
| Log directory | `logs/jabber/<user>/` (from `list_icon`) | the same, by design | see 10 and 11 |

### 1.1 Symbol strategy

How libpurple loads us (`libpurple/plugin.c:257` and `:286`):
`g_module_open(filename, G_MODULE_BIND_LOCAL)`, i.e. `RTLD_LOCAL`. The
built-in `libxmpp.so` pulls `libjabber.so.0` in as `DT_NEEDED`, also in a
local scope. With `RTLD_LOCAL` neither object's symbols enter the global
scope, so in the normal case there is no clash at all.

That is not a guarantee: the dynamic linker resolves our undefined and
interposable references against the global scope first (executable, its
`DT_NEEDED` closure, anything loaded `RTLD_GLOBAL` or via `LD_PRELOAD`).
If `libjabber.so.0` ever lands there (a third-party plugin or a scripting
loader opening it globally), default-visibility `jabber_*` calls inside our
module would silently bind to the upstream implementation with a different
`JabberStream` layout. Mitigation, cheapest first:

1. `-fvisibility=hidden` (CMake `C_VISIBILITY_PRESET hidden`). Hidden
   symbols are bound at static link time, never through the PLT, so no
   interposition is possible in either direction. `PURPLE_INIT_PLUGIN`
   already marks `purple_init_plugin` `G_MODULE_EXPORT` (default visibility),
   so no source change is needed.
2. A linker version script `src/xmpp2.map`
   (`{ global: purple_init_plugin; local: *; };`) so nothing else can leak,
   including from future static helpers.
3. `-Wl,--no-undefined` so missing libpurple/glib/libxml2/libidn link
   dependencies fail at build time, not at `dlopen`.
4. CI check: `nm -D --defined-only libxmpp2.so` must list exactly
   `purple_init_plugin`.

`-Wl,-Bsymbolic` (currently in CMake) is redundant once 1 and 2 are in place;
harmless, may stay. We deliberately do not rename `jabber_*` in the sources:
that would touch every line of 25k lines of inherited code and destroy the
ability to diff and cherry-pick upstream security fixes.

### 1.2 Signals for third-party plugin authors

The inherited signals keep their names and signatures, but
`jabber-receiving-message` (`type, id, from, to, message`) now fires from
`jabber_message_deliver()`, after the inbound pipeline (6):

- `message` is the message to show: for a carbon or a MAM result that is the
  unwrapped inner `<message/>`, not the stanza on the wire. `from`/`to` are
  its attributes. Plugins that need the wrapper use `jabber-receiving-xmlnode`,
  which still sees every raw stanza.
- It never fires for stanzas a module consumed (`X2_CONSUMED`: forged
  carbons and MAM results, duplicates, receipt/marker-only control
  messages). A deferred message (`X2_DEFERRED`, e.g. queued during a MAM
  catch-up) fires once, when it is finally delivered.
- Returning TRUE still suppresses the delivery.

## 2. Build and compat headers

- CMake, one `MODULE` target `xmpp2` producing `libxmpp2.so`, installed to
  `pkg-config --variable=plugindir purple`.
- `internal.h` is not installed by libpurple. `src/xmpp2_internal.h` replaces
  it with only what the sources use: system includes, `glibcompat.h`,
  `_()`/`N_()`, `XMPP2_PRPL_ID`, `XMPP2_WEBSITE`. Every `#include "internal.h"`
  is rewritten once (done in M0). No other private libpurple header is
  needed (`glibcompat.h` is installed).
- Translations: `_()` becomes a static inline `x2_gettext()` that tries
  `dgettext("purple-xmpp2", s)` and, when that returns `s` unchanged, falls
  back to `dgettext("pidgin", s)`. All inherited strings are translated for
  free by the distro's Pidgin catalogs; only new strings need our own `.po`.
- Dependencies: libpurple >= 2.14 (Ubuntu 24.04 ships 2.14.12, CI must
  cover it), glib 2.x, libxml2, libidn (stringprep). No farstream, no
  gstreamer, no cyrus-sasl.

## 3. Kept and dropped upstream code

Dropped in M1 (all deletions plus compile fixes, no behaviour change for
features we keep):

| Code | Lines | Why |
|---|---|---|
| `google/` (gmail notify, Google roster ext, Google session, relay, jingleinfo) | ~1.8k | Google Talk XMPP is gone. Pure dead weight and 86 `googletalk` branches in core files |
| `jingle/` and `initiate_media`/`get_media_caps` | ~3.7k | GType name clash with the built-in prpl (see 1). No DTLS-SRTP, so it does not interoperate with Conversations, Dino or Gajim calls anyway. Drags in farstream/gstreamer. Users who need Pidgin A/V keep prpl-jabber |
| `bosh.c` (XEP-0124/0206) | ~1.1k | Niche for a desktop client; Gajim does not do BOSH either. Removing it leaves one transport (TCP+TLS) in `jabber.c`, which the SM reconnect work (M10) needs |
| `auth_cyrus.c` and every `HAVE_CYRUS_SASL` branch | ~0.7k | Optional dep plus `#ifdef` noise in `jabber.c`, `auth.c`, `jabber.h`. GSSAPI users keep prpl-jabber |
| `auth_digest_md5.c` | ~0.3k | Obsoleted by RFC 6331; modern servers disable it |
| `win32/`, `Makefile.*` | | Linux-only packaging (deb, rpm, source) |

Kept: core stream, SASL PLAIN and SCRAM-SHA-1(-PLUS), roster, presence,
MUC, disco, caps, PEP (avatar, mood, tune, nick), vCard, ad-hoc commands,
in-band registration, XHTML-IM, BoB smileys, attention, ping, blocking
(XEP-0191), stream management (audited and fixed, see 7.8).

Kept for now, candidate for removal later: SI file transfer
(`si.c`, `ibb.c`, `oob.c`, XEP-0096/0065/0047/jabber:iq:oob). It is mostly
dead in modern clients, but it is the only way to talk files with other
Pidgin/Psi users and removing it touches `jabber.c`, `buddy.c`, `disco.c`.
`send_file` goes to HTTP upload first and falls back to SI only when the
server has no upload service.

## 4. Module layout

```
src/
  xmpp2_internal.h      NEW  compat for internal.h, ids, gettext fallback
  xmpp2.map             NEW  linker version script
  libxmpp.c             MOD  plugin info, prpl_info; calls x2_ext_* registry
  jabber.c jabber.h     MOD  hook calls (M1), Google/BOSH/Cyrus removal,
                             SM reconnect (M10)
  message.c message.h   MOD  delivery takes an X2Msg context (M1)
  chat.c                MOD  room disco before join, MAM-aware history (M4)
  disco.c               MOD  account bare-JID disco, server item features hook
  stream_management.c   MOD  fixes (M9), resume (M10)
  namespaces.h          MOD  all v1 namespaces added up front in M1
  (other upstream files: include swap only)
  ext/                  NEW  feature modules: libpurple glue, one file per XEP
    ext.c ext.h         hook registry and inbound/outbound pipeline driver
    x2stream.h          per-stream shared state plus one opaque slot per module
    msgctx.c msgctx.h   X2Msg: build, classify, deliver to message.c
    carbons.c           XEP-0280
    mam.c               XEP-0313 hooks, result checks, dedup
    mam_sync.c          XEP-0313 catch-up state machine, one per archive
    mam_muc.c           XEP-0313 rooms: disco before join, history, join
    statefile.c         persistence glue: path, debounce, account-removed
    receipts.c          XEP-0184
    markers.c           XEP-0333
    peerutil.c          caps policy, subscription gate, notice line (receipts, markers)
    correct.c           XEP-0308 inbound check, marker, outbound hook
    correct_cmd.c       /correct command
    upload.c            XEP-0363 (PurpleXfer glue)
    migrate.c           clone prpl-jabber accounts
  core/                 NEW  pure logic: glib + xmlnode.h only, no purple core,
                             no connection, no UI. Unit tested.
    sid.c               XEP-0359 stanza-id (with by check) / origin-id
    forward.c           XEP-0297 unwrap, XEP-0203 delay parse
    jid.c               bare-JID helpers (bare part, equality) used by the others
    carbon_parse.c      classify carbon, reject forged
    mam_proto.c         build query (form + RSM), parse <result>, <fin>
    dedup.c             bounded LRU set of message keys
    smstate.c           XEP-0198 counters (mod 2^32), bounded unacked queue
    store.c             GKeyFile archive state: cursors and key rings
    receipt_parse.c     receipts and markers element parse/build
    replace_parse.c     correction element parse/build, sender identity, marker
    corrlog.c           bounded (conversation, id) -> author log, last sent id
    upload_proto.c      slot request/response, HTTP PUT request, status line
pidgin-plugin/          NEW  optional Pidgin (GTK 2) plugin pidgin-xmpp2-ticks (7.9)
  tickmap.c             pure logic: tick states, glyphs, bounded LRU id map
  tickbuf.c             GtkTextBuffer only: marks, glyph insert, delete-range
  ticks.c               Pidgin glue: signals, correlation, prefs
  ticks_test.c          test hooks for integration-ticks (XMPP2_TEST_HOOKS only)
tests/
  unit/                 NEW  GTest per core/ file, run by ctest
  integration/          harness*.c, peer.py, test_*.sh
  server/               EXISTING Prosody 13 in podman
docs/                   this file, PLAN.md, GAJIM-COMPARISON.md
```

Layering rules (enforced in review, cheap to grep):

- `core/` includes only `<glib.h>`, `xmlnode.h`, `util.h` (for
  `purple_str_to_time`), never `connection.h`, `conversation.h`, `jabber.h`.
- `ext/` may include upstream headers. Upstream files include only
  `ext/ext.h` (and `msgctx.h` in `message.c`), never individual feature
  modules.
- New symbols use the `x2_` prefix. The inherited debug category `jabber`
  is renamed to `xmpp2` mechanically in M1; new modules use `xmpp2-<module>`.
  Tests assert only on `xmpp2*` categories.
- Each feature module stays well under 500 lines; split before it grows.
- Nothing under `src/` includes GTK or Pidgin headers; only
  `pidgin-plugin/` does, so the protocol package never depends on GTK.

## 5. Hook layer (`ext/ext.h`)

The only coupling between upstream core and new features. Added once in M1;
after that, feature milestones touch only their own files plus one line in
the registry table.

```c
typedef enum { X2_PASS, X2_CONSUMED, X2_DEFERRED } X2Verdict;

typedef struct {
    const char *name;
    void (*plugin_init)(PurplePlugin *p);      /* features, cmds, signals */
    void (*plugin_uninit)(PurplePlugin *p);
    void (*account_options)(GList **opts);     /* append PurpleAccountOption */
    void (*stream_new)(JabberStream *js);
    void (*stream_free)(JabberStream *js);
    /* disco#info of a trusted source: our domain, our server's disco#items,
     * our own bare JID (compare from with js->user) */
    void (*server_info)(JabberStream *js, const char *from, xmlnode *query);
    void (*pre_presence)(JabberStream *js);    /* roster done, before initial presence */
    void (*session_ready)(JabberStream *js);   /* PURPLE_CONNECTED */
    X2Verdict (*inbound)(JabberStream *js, X2Msg *m);
    void (*outbound)(JabberStream *js, xmlnode *message, X2Out *o);
    gboolean (*chat_joining)(JabberStream *js, JabberChat *chat); /* TRUE: hold the join */
    void (*chat_join_presence)(JabberStream *js, JabberChat *chat, xmlnode *muc_x);
    void (*chat_joined)(JabberStream *js, JabberChat *chat);  /* once per join */
    void (*stream_closing)(JabberStream *js);  /* top of jabber_close() */
    void (*carbons_settled)(JabberStream *js); /* carbons enable answered */
} X2Ext;
```

`stream_closing` (M3) runs first thing in `jabber_close()`, while buddies,
chats and the connection still exist: the last moment a module can deliver
queued messages or save state. `stream_free` runs after most of
`JabberStream` is gone. `carbons_settled` is dispatched by
`x2_ext_carbons_settled()`, which the carbons module calls once per stream
after its enable IQ was answered (result or error), or at once without
server carbons; the MAM module waits for it when the server offers carbons
and a module named `carbons` is registered (`x2_ext_registered()`), else it
queries at `session_ready`. If it never comes, the MAM watchdog sends the
query anyway after 30 s.

Call sites in upstream code (M1): `jabber_stream_new/free`, the disco chain
in `disco.c` (server info, then a new own bare-JID `disco#info`, then roster),
`jabber.c` just before `jabber_presence_send(js, TRUE)` and after
`purple_connection_set_state(..., PURPLE_CONNECTED)`, top of
`jabber_message_parse`, `jabber_message_send`, and the self-presence branch
in `chat.c`. `JabberStream` gains one field: `X2Stream *x2`.

`server_info` only ever sees disco#info results of entities we trust: our
server domain, JIDs listed in our own server's `disco#items` (recorded per
stream by `x2_ext_server_item_add()` in `jabber_disco_server_items_result_cb`),
and our own bare JID. Results for contacts' caps, user service browsing and
remote MUC services never reach it, so a foreign domain advertising upload
or carbons is never used. It may run more than once for the same JID; hooks
must be idempotent. `x2_ext_server_info()` itself marks the own bare JID as
a trusted archive when it advertises `urn:xmpp:sid:0` or `urn:xmpp:mam:2`
(7.3).

Per-stream module state: `X2Stream` has only shared fields (dedup set,
server items, trusted archives) and `gpointer priv[X2_EXT_MAX]`, one slot
per registry position. A module calls `x2_ext_get_priv(js, &x2_ext_<me>)` /
`x2_ext_set_priv()`, allocates in `stream_new` and frees and clears in
`stream_free` (a slot left set is reported as a leak). Adding a module
therefore touches only its own files plus the registry table and the
CMake source list.

Login sequence after M1: bind, SM enable, session, server `disco#items`
and `disco#info`, own bare-JID `disco#info` (MAM support lives there on
Prosody), roster, `pre_presence` (MAM starts buffering), initial presence
(offline messages start flowing), `session_ready` (carbons enable, upload
discovery), carbons enable IQ result (or error, or no carbons support), then
the MAM catch-up query. One extra round trip at login, in exchange for knowing MAM
support before offline messages arrive.

## 6. Inbound message pipeline

Every `<message>` goes through one path. The context object:

```c
typedef struct {
    xmlnode   *outer;        /* stanza as received (borrowed from the parser) */
    xmlnode   *msg;          /* the message to show (inner for carbons/MAM) */
    gboolean   owns_outer;   /* TRUE for x2_msg_dup() copies */
    int        stage;        /* registry index of the module that saw it last */
    X2Source   src;          /* LIVE, CARBON, MAM */
    X2Dir      dir;          /* IN, OUT (sent by us from any device) */
    time_t     ts;           /* <forwarded><delay>, else inner delay, else now */
    gboolean   delayed;
    char      *archive;      /* own bare JID, or room JID for groupchat (also LIVE) */
    char      *stanza_id;    /* trusted only: by == archive and archive supports sid/mam */
    char      *key;          /* dedup key, see 7.3 */
    char      *peer;         /* conversation key: bare JID, or full for MUC PM */
} X2Msg;
```

`peer` is the full JID for a private message from a room occupant: the room
is joined, or the message carries `<x xmlns='http://jabber.org/protocol/muc#user'/>`
(XEP-0045 PMs, e.g. fetched from MAM before the join). `ts`: carbons and MAM
put the send time on `<forwarded><delay/>`, which wins over a delay on the
inner message. `message.c` takes `sent`/`delayed` from the context only.

Ownership and `X2_DEFERRED`: the context built for a received stanza borrows
it from the parser and is freed when the pipeline returns, whatever the
verdict. A module that keeps a message takes `x2_msg_dup(m)` (deep copy of
`outer` with `xmlnode_copy()`, `msg` relocated into the copy, owns both) and
returns `X2_DEFERRED`. Later it hands the copy to `x2_msg_redeliver(js, d)`,
which runs the inbound hooks after the deferring module (`stage`), delivers
if they all pass, and frees the copy in every case (a later module may
defer it again by taking its own dup).

Order of the inbound chain (registry order matters and is fixed):

1. `carbons`: if the outer stanza carries `urn:xmpp:carbons:2`
   `received`/`sent`, verify outer `from` equals our bare JID exactly, and
   for `sent` the inner `from` bare JID equals ours, for `received` the inner
   `to` bare JID equals ours (else drop and log: forged carbon). Unwrap, set
   `src=CARBON`, `dir`.
   Then `receipts-answer` (second table entry of `ext/receipts.c`): answer a
   receipt request of a live message (rule in 7.4) before `mam` can queue
   it, or drop it as the duplicate of an archive result shown during the
   catch-up (it would never be answered after that).
2. `mam`: if it carries `urn:xmpp:mam:2` `<result>`, verify outer `from` is
   the archive we queried (own bare JID or the room) and `queryid` matches
   an active query (else drop). Unwrap `<forwarded>` and check the inner
   message against the archive: account archive requires inner `from` or
   `to` bare JID to equal ours; room archive requires inner `from` bare JID
   to equal the room JID and always takes the MUC delivery path, whatever the
   inner `type` says (else drop and log: forged MAM result). Take `ts` from
   the `<forwarded><delay>`, `stanza_id` from `result@id`, `src=MAM`, `dir`
   from comparing inner `from` with our bare JID (or our nick/occupant-id in
   MUC).
3. `mam` (dedup and ordering): drop if `key` already seen. If the archive is
   in `SYNCING` and `src` is LIVE or CARBON, queue an `x2_msg_dup()` and
   return `X2_DEFERRED` (ownership: see above). The
   queue is flushed to the UI (with dedup) on `LIVE`, on watchdog expiry and
   on stream teardown: a duplicate beats a lost message.
   An archive result whose key (stanza-id or origin-id) matches an undelayed
   message in the queue is dropped: a message archived after the catch-up
   began but before the server ran the query comes both ways, and the live
   copy is shown as it came, never flagged delayed.
4. `correction`, `upload`, `receipts`, `markers` (table order): rewrite
   corrections, consume pure control messages (`<received/>`,
   `<displayed/>` without body).
5. Deliver: `msgctx` calls the upstream handlers in `message.c` with the
   context. `dir=OUT` writes into the IM conversation for `peer` with
   `PURPLE_MESSAGE_SEND | PURPLE_MESSAGE_REMOTE_SEND`; `dir=IN` keeps the
   existing `serv_got_im`/`serv_got_chat_in` path with `ts` and
   `PURPLE_MESSAGE_DELAYED` when `delayed`. Chat states are ignored unless
   `src=LIVE && dir=IN && !delayed`.
6. After delivery, `mam` records the key and, if the archive is `LIVE`,
   advances the cursor.

Outbound (`jabber_message_send` -> `outbound` hooks): add `<origin-id>`
(XEP-0359, a fresh UUID, also used as the stanza `id`), `<request/>`
(receipts, chat/normal only), `<markable/>` (1:1 only), and record the key
in the ring so a later MAM catch-up does not show our own message twice.

## 7. Feature data flows

### 7.1 Carbons (XEP-0280)

`server_info`: note `urn:xmpp:carbons:2`, only from our server domain or our
own bare JID (never from a component). `session_ready`: send
`<enable/>`. The account-archive MAM query is sent only after the enable IQ
result (or error) arrives, signalled by `x2_ext_carbons_settled()` (called at
once when the server has no carbons); querying earlier opens a window in which a message
from another device is neither in the catch-up nor copied to us. Inbound handled in step 1 above. Sent carbons appear as outgoing
lines in the right conversation; received carbons appear as normal incoming.
Validation lives in `core/carbon_parse.c`: outer `from` absent or exactly our
bare JID, one `<sent/>` or `<received/>`, inner type chat or normal, the
own-side inner JID is ours (`from` for sent, `to` for received). Failures are
dropped with the `xmpp2-carbons` line "forged carbon dropped". No carbons
`<private/>` or `<no-copy/>` is added to our sends.
No receipts or markers are sent in response to a carbon (the receiving
device does that).

### 7.2 MAM (XEP-0313) and catch-up

Per archive (the account archive, one per joined MAM room) a small state
machine lives in `ext/mam.c`:

```
IDLE --start--> SYNCING --fin complete--> LIVE
                  |  ^ fin incomplete: next page (after=<last>)
                  |  item-not-found on after=cursor: retry with start=cursor_ts
                  +--error--> LIVE (flush queue, log, notify once)
                  +--watchdog: 30 s without a result or fin--> LIVE
                       (flush queue, one system notice "history incomplete")
```

Stream teardown in any state flushes the queue to the UI before the
`JabberStream` is freed (`stream_closing` hook). Implementation (M3):
`ext/mam.c` (hooks, result checks, dedup), `ext/mam_sync.c` (this state
machine), `ext/statefile.c` (9), `core/mam_proto.c`, `core/store.c`. Debug
category `xmpp2-mam`. Test hook (compiled in only with
`cmake -DXMPP2_TEST_HOOKS=ON`, as in CI; release builds ignore the
variable): with `XMPP2_TEST_MAM_STALL` set in the environment the query is
built and logged but never sent, so the watchdog and teardown paths can be
tested. ctest passes `XMPP2_TEST_HOOKS=1|0` to every test; the
stall parts exit 77 (skipped) without it.

Queries (`core/mam_proto.c`): `urn:xmpp:mam:2` form with optional `start`
and `with`, RSM `<max>` 100 and `<after>`/`<before/>`. Page responses are
`<result>` messages followed by the IQ `<fin complete=... ><set><first/>
<last/></set></fin>`. Results arrive before `<fin>` on the same stream, so
when `<fin>` is processed every result of that page is already delivered.

Account archive, after the carbons enable result (follows Gajim's `mam.py`):

- Cursor stored: `after=<cursor>`, page until `complete='true'`, at most
  `mam_max_pages` (20, i.e. 2000 messages). Beyond the cap: stop, jump to the
  last page (`<before/>`, `max=100`) and write one system notice "older
  history skipped" into each affected conversation.
- No cursor, fresh account: `start = now - mam_initial_days` (account
  option, default 1; Gajim uses 7 but Pidgin opens a window per
  conversation). 0 means cursor-only.
- No cursor, cloned account (`[meta] initial=cursor-only`): fetch only the
  latest id (`<before/>`, `max=1`) to set the cursor, display nothing, so
  history prpl-jabber already logged is not replayed.
- `item-not-found` (cursor pruned from the archive): retry once with
  `start=cursor_ts`, skipping any result whose id equals the old cursor
  (the `start` bound is inclusive), else apply the first-run rule.
- Empty first page after a cursor: Prosody's internal storage answers an
  unknown `after` id with an empty, complete page instead of
  `item-not-found`. When the account advertises `urn:xmpp:mam:2#extended`,
  an empty first page triggers a probe (`ids=<cursor>`, `max=1`, results not
  shown); cursor not found means the same retry as `item-not-found`. Known
  limit: when the cursor id is corrupt rather than pruned, the message at
  `cursor_ts` itself can show once more (its real id differs from the stored
  one).
- Offline storage (XEP-0160) overlap: an offline copy that the archive also
  returned is caught by its stanza-id (`S:` key) at flush time. Only after
  a complete catch-up that ended on the newest page because of the page cap
  are queued messages that are delayed and carry a trusted stanza-id
  dropped without being seen: they lie in the part the cap skipped on
  purpose, and showing them after the newest page would break ordering
  (`x2_mam_queued_fate()`). In every other case they are shown: dropping
  them could only lose messages.
- Incomplete catch-up (error, watchdog, teardown): the cursor is frozen for
  the rest of the session, live messages no longer move it, so the next
  login fetches the gap again. Every message shown while the cursor is
  frozen also has its key put in the persisted ring, so the refetched gap
  shows nothing twice. After an error, the watchdog or a full queue there
  is always one notice: account-level (`purple_notify_message()`) for the
  account archive, plus a line in each conversation that had queued
  messages; one line in the room for a room archive.

Rooms (M4), on join: disco#info the room before sending the join presence
(cached per stream; Gajim does the same). If the room advertises
`urn:xmpp:mam:2`, join with `<history maxchars='0' maxstanzas='0'/>` (some
servers honour only one of the two). `chat_joined` fires once per join, on
the first self-presence carrying status code 110, never on a nick change or
a later self-presence update; it then queries the room archive (`to=room`):

- Cursor stored and newer than `mam_muc_threshold_days` (default 2):
  `after=<cursor>`, paged, at most 10 pages; beyond that jump to the last
  page and write one system line "older history skipped".
- Otherwise: last page only (`<before/>`, `max=50`), plus the same system
  line when the `<fin/>` says the page is not the whole archive
  (`complete='false'`). This mirrors classic MUC history but without
  replaying it on every rejoin. If a stale cursor is still inside that page,
  the messages up to it were shown before: they are dropped and there is no
  notice.
- Room without MAM: unchanged upstream behaviour (legacy history).

Implementation (M4): `ext/mam_muc.c`. `jabber_join_chat()` asks the
`chat_joining` hook, which sends the room `disco#info` and holds the join
(at most 5 s; on timeout the join goes ahead as a room without MAM); the
answer is cached per stream (errors are not, a room created by the join is
asked again next time), marks the room trusted for stanza-ids
(`x2_ext_archive_trust()`) when it advertises `urn:xmpp:mam:2` or
`urn:xmpp:sid:0`, and releases the join through `jabber_chat_send_join()`,
whose `chat_join_presence` hook replaces any user `<history/>` with
`maxchars='0' maxstanzas='0'` for a MAM room. `presence.c` resets
`chat->joined` on an unavailable self-presence only when it is not a nick
change (status 303), so `chat_joined` fires once per join. Each room has its
own `X2MamArchive` (the same state machine as the account archive in
`ext/mam_sync.c`: queue, watchdog, item-not-found retry, probe, frozen
cursor), page size 50, cap 10 pages, cursor in the state file section
`[archive <room>]`. Room results are accepted only from the room's bare JID
with the room's active queryid and an inner `from` of the room, are always
delivered as groupchat into the room (`x2_mam_room_result_check()` forces
the inner `type`), and are never treated as 1:1. Results of a last-page
query are held until its `<fin/>` decides the notice, and are capped at the
`max` asked for (`x2_mam_page_limit()`): results beyond it are dropped as
forged, with a debug line, and do not restart the watchdog. Live groupchat of a
MAM room is deduplicated (`S:` key of the room's stanza-id) and queued while
the room syncs; rooms without MAM are not touched. Our own messages sent
from another device come back from our nick and are marked sent by
libpurple. MUC private messages are 1:1 messages of the account archive:
the `muc#user` hint keeps the occupant full JID as the conversation.

Ordering: Pidgin can only append to a conversation. While an archive is
`SYNCING`, live and carbon messages for it are queued and flushed in arrival
order after the last page, dedup applied at flush time. This also absorbs
the XEP-0160 offline flood that Prosody delivers right after initial
presence, because the account archive enters `SYNCING` in `pre_presence`.
Messages without a `<body/>` (chat states, receipts, markers, a room
subject) are not queued: they pass at once. The queue holds at most 5000
messages per archive; one more ends the catch-up as incomplete (frozen
cursor, "history incomplete" notice) and delivers the queue.

Cursor updates: after every `<fin>` (cursor = `last`, timestamp = last
delivered `ts`), and for live messages only once the archive is `LIVE`
(Gajim's `is_catch_up_finished` rule). Never from a message that lacks a
trusted `stanza-id`.

Delivery guarantee: at least once across a crash, bounded by the 2 s write
debounce (see 9); exactly once otherwise.

### 7.3 Dedup keys

- `S:<archive bare JID>:<stanza-id>` when the message has a trusted
  stanza-id (stanza-ids are unique within one archive only, so two archives
  that issue the same id do not collide; XEP-0359:
  `by` equals the archive JID and the archive advertised `urn:xmpp:sid:0` or
  `urn:xmpp:mam:2`; otherwise stanza-ids are spoofable and ignored). Every
  message has an archive, live ones included: our own bare JID, or the room
  bare JID for `type='groupchat'`. Trust is a per-stream flag per archive:
  the own bare JID gets it from the account `disco#info`
  (`x2_ext_server_info()`), a room from its `disco#info` before the join
  (M4, `x2_ext_archive_trust()`). Without it a remote sender could forge
  `<stanza-id by='me'/>`; a server that advertises sid strips such forgeries.
- Otherwise `O:<dir>:<peer>:<origin-id>`. Every message we send carries an
  origin-id (6), so this covers our own sends.
- Otherwise no key: the message is never treated as a duplicate. The stanza
  `@id` is never used: it cannot tell our ids from foreign ones.
- Inner keys (`O:`) are recorded only for messages delivered without a
  trusted stanza-id, and a MAM result consults them in addition to its
  `S:` key. This recognises in a later catch-up a message first seen live
  without a stanza-id (our own sends; Prosody does not stamp sent carbons),
  without letting an attacker-chosen origin-id suppress an archived message.

Two stores: an in-memory LRU of 4096 keys per stream (`core/dedup.c`), and a
persisted ring of the last 256 non-`S:` keys per archive in the state file,
which covers exactly the messages the cursor cannot (own sends and sent
carbons after the last received stanza-id). Ring entries are opaque strings:
one written by an older version (`S:<id>` without the archive) never matches
a new key and ages out of the ring.

### 7.4 Delivery receipts (XEP-0184)

Implemented in `ext/receipts.c` (`core/receipt_parse.c`). Outgoing:
`<request/>` on 1:1 messages with a body, unless the recipient resource is
known from caps and lacks `urn:xmpp:receipts`, or is a room occupant
(unknown caps count as supported); the (bare JID, id) pair is remembered in a
256 entry set. Incoming request (answered by the `receipts-answer` entry,
which runs before `mam`, so a live message queued or deduplicated during
the MAM catch-up is still answered, on arrival): reply
`<received id=.../>` to the full JID only when `src=LIVE` and `dir=IN`
(a stanza from the server's offline store, `delayed`, is still LIVE: its
sender is waiting for the receipt, so it gets it at login; MAM results and
carbons never do), `type!='groupchat'` and the sender is in the roster with
presence subscription (no presence leak to strangers; same as Gajim; the
check is `x2_peer_sees_us()`, shared with markers). Incoming `<received/>`: emit the purple
signal `xmpp2-receipt` (account, bare peer, id), only for a (peer, id) we
asked for and only when `x2_msg_is_live_incoming()`. Pure receipts are
consumed whatever their source. Pidgin 2 cannot decorate an
already written line, so v1 has no visual ticks; an account option
"Show delivery and read notices" (default off) writes a short
`PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_NO_LOG` line. The optional Pidgin
plugin `pidgin-xmpp2-ticks` renders ticks from the signals (7.9). For it,
`receipts_outbound` also emits `xmpp2-sent-message` (account, bare peer, id,
plain body) for every outgoing 1:1 message with a body and an id, whatever
the caps say, before the request decision; it fires while libpurple sends,
before the UI writes the sent line.

### 7.5 Chat markers (XEP-0333)

Implemented in `ext/markers.c`. Outgoing messages get `<markable/>` under the
same caps rule as receipts. Incoming markable messages (live and MAM, not
carbons: the other device marks those) record the newest per contact (by
`ts`); a `displayed` is sent to the bare JID at most once per remembered id,
and only to contacts with presence subscription FROM (`x2_peer_sees_us()`,
the receipts gate): only those are recorded, in a table capped at 256
contacts that drops the least recently updated one.
Sending `displayed`: connect to
`conversation-updated` with `PURPLE_CONV_UPDATE_UNSEEN`; Pidgin sets the
conversation data `unseen-count` to 0 when the user focuses the window
(`pidgin/gtkconv.c` 7860, 8491). At that moment send one `<displayed id>` for
the newest markable incoming message of that 1:1 conversation (including
ones fetched by MAM). 1:1 only in v1. Incoming markers: signal `xmpp2-marker`
(account, bare peer, id, marker name) plus the same optional notice, under
the same "asked for" rule as receipts. Known limit: a message read in an
already focused window never resets the unseen count, so it is not marked
until the next reset. Read-state sync from our other devices
(sent `displayed` carbons, XEP-0490) is later.

### 7.6 Message correction (XEP-0308) (implemented)

Module `correction` (`ext/correct.c`), after `mam` in the inbound chain, so
live, carbon and MAM messages are treated alike and only once. Every message
with a body is recorded in a bounded log (2048 ids per stream, oldest
dropped first) as (conversation `peer`, id) -> author, for `id`,
`origin-id` and the trusted `stanza-id`. The first author of an id stands.
The author is the bare JID in 1:1 and, in a room, the XEP-0421 occupant-id
when present and the room advertises `urn:xmpp:occupant-id:0` in its
`disco#info` (cached by `mam_muc.c`, `x2_mam_room_has_occupant_id()`: a room
that does not advertise it does not strip forged ids), else the full JID
(room/nick).

Incoming `<replace id=X>` with a body is accepted only if X was recorded in
the same conversation from the same author. An accepted correction is
shown as a new line whose text is prefixed with `corrected: ` (the body is
rewritten in the stanza before delivery, the XHTML-IM alternative is
dropped so it cannot hide the marker), and logged like any message, since
libpurple cannot edit history. A correction of an unknown id (not seen in
this session or in the MAM data fetched so far) or by another author is
shown as a plain message without the marker, with a warning in the
`xmpp2-correct` debug category. Corrections themselves are never recorded as
originals, so a later correction still names the first message.

Outgoing: the `correction` outbound hook remembers the id of the last
message we sent per conversation (1:1 and rooms; also taken from sent
carbons of our other devices). `/correct TEXT` (`ext/correct_cmd.c`,
registered for IM and chat) sets that id as `pending`, sends TEXT through
the normal send path (shown and logged as a sent line) and the hook adds
`<replace id=last/>`. A correction is not itself remembered as the last
message, so repeated `/correct` always names the original. Without an
earlier message the command fails with a message. Advertises
`urn:xmpp:message-correct:0`.

### 7.7 HTTP File Upload (XEP-0363)

Implemented in `core/upload_proto.c` and `core/upload_http.c` (pure logic,
unit tested), `ext/upload.c` (discovery, prpl entry points),
`ext/upload_xfer.c` (the `PurpleXfer`, slot request, result handling) and
`ext/upload_put.c` (the streaming HTTP PUT client).

Discovery: in `server_info`, a JID that is our server domain or an item of
our own server's `disco#items` (never our own bare JID, foreign upload
services are never offered) advertising `urn:xmpp:http:upload:0`, with
`max-file-size` from its x-data. The first service wins.

`send_file`, `new_xfer` and `chat_send_file` (rooms) create a `PurpleXfer`
(type SEND) with our own ops when a service exists, else they fall back to
the inherited SI transfer (`can_receive_file` is TRUE whenever a service
exists: the URL reaches anybody). On init the size is checked against
`max-file-size`, before any network request, and an empty file is refused.
There is no limit of our own: the file is streamed, so the server's
`max-file-size` (none announced = none) is the only cap. Then:

1. IQ get `<request filename size content-type/>` to the service (content
   type from a small extension table, else `application/octet-stream`). The
   answer must come from the discovered service.
2. `<slot>`: `<put url>` with optional headers and `<get url>`. Only
   `Authorization`, `Cookie` and `Expires` are used (other names are
   dropped, counted and logged as a number only). A kept header value with
   CR, LF or any other control character fails the whole slot (header
   injection). Header values are trimmed. Both URLs must be http(s), carry
   no user info, no IPv6 literal, no spaces or control characters, and must
   be https unless the account option `allow_plain_http_upload` ("Allow
   unencrypted file upload", default off) is set.
3. `PUT`, streamed from disk by `ext/upload_put.c`, a minimal HTTP/1.1
   client of our own (libpurple's fetch API can only send a request that is
   one buffer in memory). The head is built by `x2_upload_put_head()`
   (`Host`, `User-Agent`, `Content-Type`, `Content-Length`, the allowed slot
   headers, `Connection: close`; no `Expect`, no chunked encoding). The
   connection is `purple_proxy_connect()` for `http` (so the account proxy
   applies) or `purple_ssl_connect()` for `https`: libpurple's SSL plugin and
   its certificate verification (the `x509_tls_cached` verifier, the same as
   for the XMPP stream), with the URL host as server name (SNI) and
   verification name, port from the URL (default 443/80). One watcher
   (`purple_input_add()` on the socket, `READ | WRITE` while writing, `READ`
   afterwards) drives everything. Per main loop turn at most 256 KiB are
   written: first the head, then chunks of 64 KiB read from the file with
   `fread()` into one buffer. A write that returns EAGAIN keeps the buffer and
   offers the same bytes again on the next turn (GnuTLS requires that), so a
   slow server slows the read and memory stays one chunk (plus the stdio
   buffer) whatever the file size. `READ` is watched during the write so an
   early answer (413, 401) is seen at once; when the socket refuses our data
   (EPIPE, reset) the response is still read before the failure is reported.
   The status line is read by `x2_upload_resp_feed()` (interim 1xx
   responses are skipped, the rest of the response is ignored). 200, 201 and
   204 are success (`x2_upload_put_status_ok()`: XEP-0363 says 201, some
   servers differ). A 3xx is never followed (it could lead to plain http or
   to another host, with our slot headers) and fails with its own "redirected"
   message; 401 and 403, 413 and any other status have their own messages. A
   file that shrinks while it is read fails the upload ("the file changed").
   The logic that can be tested without sockets is in `core/upload_http.c`:
   the accounting of head, body and sent bytes (`X2UploadSend`), the progress
   steps and the response parser. Not done: a timeout for a connection that stalls (the user can cancel in
   the transfer window), and `Expect: 100-continue`.
4. Send `<message type='chat|groupchat'>` with the GET URL as body plus
   `<x xmlns='jabber:x:oob'><url/></x>` through the normal outbound path
   (`x2_msg_prepare_outgoing`: origin-id, hooks), echo it locally for 1:1
   (a room reflects it), complete the xfer.

Progress: when the connection is up the `PurpleXfer` is started with
`purple_xfer_start(xfer, -1, NULL, 0)` (no descriptor for libpurple: it only
sets status and start time, which the transfer window uses for its rate and
"waiting" text), then `purple_xfer_set_bytes_sent()` and
`purple_xfer_update_progress()` after every percent of the file (at least
every 64 KiB, and at the end), counting bytes accepted by the socket. The
transfer window's Cancel button runs `purple_xfer_cancel_local()`, which frees
the upload (`cancel_send`): watcher removed, socket (or SSL connection)
closed, file closed; a callback from inside the PUT's own progress call is
safe (the PUT is freed when the callback returns).

Errors reach the user through `purple_xfer_error()` ("File upload of X
failed: reason") and cancel the transfer; the same text is logged under
`xmpp2-upload`. Logging: never the slot URLs (signed tokens) or headers; the
only URL data in our log is the PUT host and scheme. Our own client logs no
URL at all (libpurple's fetch code logged the request head and, with an
unbounded response, the full URL; it is no longer used). The XML console of
the stream still shows the slot IQ, as for any stanza.

Memory: one 64 KiB buffer, the request head and the 16 KiB response window,
independent of the file size (the integration tests upload 50 MiB with a peak
RSS growth below 16 MiB, in practice a few hundred kB). The file is read with
blocking `fread()` calls of one chunk each, so a file on a stalled network
file system blocks the UI for that read. In-flight uploads live in the
module's per-stream state (not in `js->file_transfers`, whose entries the SI
code casts to its own struct) and are cancelled in the `stream_closing`
hook, while `js->iq_callbacks` still exists (a slot IQ callback is removed
on cancel); `stream_free` only checks that none is left.
Receiving needs nothing new: `jm_body_with_oob()` already shows the URL.

### 7.8 Stream management (XEP-0198), audit result

Upstream `stream_management.c` implements counting and acks only. Findings
(upstream):

1. No resumption: sends `<enable/>` without `resume='true'`, ignores
   `<enabled id max>`, never sends `<resume>`.
2. Unacked queue is keyed by bare JID and survives the `JabberStream`; on the
   next login `jabber_sm_enable()` blindly resends everything (stale IQs,
   presence, and messages the server may already have) on a new session.
3. `<r/>` is sent after every outbound stanza (doubles traffic).
4. `h` handling ignores 32-bit wrap and never validates `h` against
   `sm_outbound_count`.
5. `<failed/>` drops the queue silently, losing unacked messages without
   telling the user; the XEP-0198 1.6 `h` attribute on `<failed>` is ignored.

M9 (done) fixes 2 to 5 (M10 fixes 1, below), contained in
`stream_management.c` plus the pure `core/smstate.c`:

- State (counters, unacked queue, `<enabled id max resume location>`) lives
  in a table keyed by `PurpleAccount`, not in `JabberStream`, so M10 can
  keep it across a transport reconnect. `jabber_close()` frees it via
  `jabber_sm_close()`; `JabberStream` keeps only `sm_state` (and, since
  M10, `sm_resume` while a resumption runs).
- `<enable/>` was sent without `resume='true'` in v0.1.0 (tech lead
  decision: the server must not hold a session we never resume); since M10
  it carries `resume='true'` unless the account option `sm_resume` is off.
  `<enabled id max resume location>` is parsed and logged (debug
  `xmpp2-sm`).
- In v0.1.0 only `<message>` stanzas with a body and a `to` were copied
  into the unacked queue; since M10 every stanza is copied (a resumption
  resends all of them). Only messages with a body are ever reported.
- Raw XML from `send_raw` (the XML console) is parsed while SM is on (a
  wrapper element allows several top-level stanzas in one write) and each
  stanza is counted like any other, else the server's `h` overtakes ours and
  the stream ends with `handled-count-too-high`. Input that is not a
  sequence of complete elements is refused (not sent) with an error
  dialog while SM is on.
- Counters wrap modulo 2^32. Each queued stanza carries its sequence number,
  so an ack trims exactly the stanzas in `(acked, h]` even after the queue
  overflowed. An `h` beyond what was sent ends the stream with
  `handled-count-too-high` and a connection error.
- The queue is bounded at 1000 stanzas; overflow evicts the oldest and is
  counted ("untracked" in the debug log).
- `<r/>` goes out after 5 unrequested stanzas or 2 s after the last stanza
  (idle timer), only once `<enabled/>` arrived; a server `<r/>` is answered
  at once. A 20 message burst costs 4 `<r/>` instead of 20.
- `<failed h='N'/>` trims exactly N (logged), then drops the rest of the
  queue: without a session those stanzas went over a plain stream.
- Every `<enable/>` starts from zero and never resends an earlier queue.
- On `connection-error` every unacked `<message>` with a body is written to
  its conversation as a system/error line "Connection lost: this message
  may not have been delivered: TEXT"; then the queue is emptied. Tech lead
  decision for v1: the user is told instead of an automatic resend; the
  MAM-checked resend once planned here is left for M10/M3 follow-up if
  wanted. A clean sign-off reports unacked messages the same way ("Signed
  off before the server confirmed this message, it may not have been
  delivered: TEXT"): the stream close does not wait for a last ack.

#### M10: resumption as a silent reconnect

Any `purple_connection_error` tears down buddies and chats, which the server
does not send again after a resume, so resumption happens inside one
`PurpleConnection` that stays `PURPLE_CONNECTED`. Files: `sm_resume.c`
(transport lifecycle), `stream_management.c` (`<resume/>` state, ids),
`core/smresume.c` (attempt policy) and `core/smstate.c`
(`x2_sm_state_resumed()`), all with debug category `xmpp2-sm`.

- Every transport failure in `jabber.c` (read or write error, server closed
  the socket, ping timeout, a failed connect or TLS handshake of an
  attempt) goes through `jabber_stream_lost()`. A deliberate close
  (`jabber_close()`: sign-off, account removal, quit) never does.
- A loss resumes only when the account is fully connected, SM is enabled
  with `resume='true'` and an id, and no error is pending. Then
  `js->sm_resume` is set; while it is set, stanzas sent by anything (UI,
  modules, XML console) are counted and queued but not written, `<r/>` and
  `<a/>` are not sent, the keepalive is off and connect progress is not
  reported (the UI keeps showing the account online).
- The teardown (`jabber_transport_close()`: socket or TLS, parser, write
  buffer, SASL state, keepalive and inactivity timers) runs from a 0 s
  timeout, never inside the parser. Roster, buddies, chats and IQ
  callbacks stay.
- Bounded: 2 attempts, the first after 1 s, the second 5 s after the first
  failed; each attempt has 20 s to reach `<resumed/>`; no attempt starts
  after the window, which is the server's `max` (60 s if absent, capped at
  600 s).
- An attempt runs the normal connect (SRV, TCP, STARTTLS, SASL). The
  post-auth `<stream:features/>` go to `jabber_sm_resume_features()`: with
  SM offered it sends `<resume previd h/>` (our inbound count) instead of a
  bind; without SM it gives up.
- `<resumed h/>`: `h` must match `previd` and must not exceed what was sent
  (modulo 2^32). The queue is trimmed to `h`, the rest (stanzas lost in
  flight, then those held during the gap) is written again in order with
  its original sequence numbers (raw, not counted again), then `<r/>`. A
  status change made during the gap is sent after that. No bind, no
  roster fetch, no initial presence, no `x2_ext_session_ready()`: MAM does
  not catch up again, carbons stay enabled on the server, rooms stay
  joined.
- `<failed/>` (debug `resume failed (condition)`), an invalid `<resumed>`,
  an unacked stanza evicted from the full queue, no attempts left or the
  window over: `h` on `<failed>` is applied, then the usual
  `purple_connection_error` (network error). The `connection-error`
  handler reports the unacked messages as in v0.1.0, the UI reconnects
  with a full login (bind, roster, rooms rejoined by Pidgin), MAM fills
  the gap.
- `jabber_close()` cancels the attempt and every timer first. A clean
  sign-off sends `</stream:stream>`, which ends the session on the server
  (Prosody does not hibernate it; `integration-sm_resume` checks that the
  room sees us leave at once). A sign-off during a resumption cannot reach
  the old session: the server drops it after `max`.
- NetworkManager: on a "network down" event Pidgin calls
  `purple_account_disconnect()` on every account (`pidgin/gtkconn.c`,
  `pidgin_connection_network_disconnected()`), and on "up" it signs them on
  again. For libpurple that is a deliberate disconnect: no resumption, the
  closing `</stream>` usually does not reach the server, so the server
  keeps the old session until `max` or until our next login replaces the
  resource; the new login is a full one with MAM catch-up. Resumption
  covers drops the socket notices first (Wi-Fi roaming, suspend without NM,
  a NAT timeout, a server-side TCP reset) and ping timeouts.
- Test hook (`-DXMPP2_TEST_HOOKS=ON`): `XMPP2_TEST_SM_RESUME_DELAY_MS`
  stretches the first delay so a test can act while the transport is down.

MAM, not SM, is the primary offline-messaging mechanism.

### 7.9 Delivery and read ticks in Pidgin (`pidgin-xmpp2-ticks`)

Optional Pidgin 2 plugin (id `gtk-xmpp2-ticks`, file
`pidgin-xmpp2-ticks.so`, sources in `pidgin-plugin/`), built when CMake finds
`pidgin.pc` (`XMPP2_PIDGIN_PLUGIN=AUTO|ON|OFF`), installed into
`pkg-config --variable=plugindir pidgin`, packaged separately. It exports
only `purple_init_plugin` (same version script as the prpl) and is not linked
with `--no-undefined`: `pidgin_*` symbols come from the executable.

Pidgin 2 cannot rewrite a written line, but its conversation view
(`GtkIMHtml`) is a `GtkTextView` that only appends, so the plugin keeps a
`GtkTextMark` at the end of each sent line and later inserts a glyph there
with `gtk_text_buffer_insert_with_tags()`.

Correlation of a message id with its line. libpurple's `common_send()`
calls `serv_send_im()` and then `purple_conv_im_write()` in one call stack,
and emits `sent-im-msg` at the end:

1. `xmpp2-sent-message` (prpl, from `receipts_outbound`) stores
   (account, peer, id) as *pending* and adds a `G_PRIORITY_HIGH` idle that
   drops it.
2. `displayed-im-msg` (Pidgin, after `pidgin_conv_write_conv()` appended the
   line) with `PURPLE_MESSAGE_SEND`, the same account and the same
   normalized peer as the conversation: create an anonymous left-gravity mark
   at the buffer end, map id -> mark, clear pending.
3. `sent-im-msg` clears pending too (send failed, or another plugin
   suppressed the display). Because pending never survives to the next main
   loop iteration, a sent line written later without a send (a carbon of our
   other device, a `serv_got_im()` echo) is never tagged with a stale id.

Updates. `xmpp2-receipt` gives delivered, `xmpp2-marker` gives delivered for
`received` and displayed for `displayed` or `acknowledged`; a state never
goes back. The conversation is found with
`purple_find_conversation_with_account()` (the prpl's normalize makes bare
and full JID names meet); the glyph (" ✓", " ✓✓", or the ASCII or
word style) replaces the previous one only if that text is still intact at
the mark, in a grey or blue tag at `PANGO_SCALE_SMALL`. Glyphs are display
only: never logged, never sent.

State and lifetime. One `X2TickBuf` per view, stored as object data on the
`GtkIMHtml` and freed from its `destroy` handler (window or tab closed,
conversation destroyed); contact-merged IMs share it. It holds at most 200
ids, least recently used dropped, deleting their marks. Lines that leave the
buffer leave the map: a `delete-range` that covers a mark
(start < mark <= end: scrollback trimming in `pidgin_conv_write_conv()`,
`gtk_imhtml_clear()` for "Clear Scrollback") forgets the id, and
`cleared-message-history` clears the map. A range that starts exactly at a
mark keeps it: that is Pidgin's typing notice, appended after the last line
between `typing-notification-start` and `-end`; when a glyph goes where
that notice starts, the start mark is moved past the glyph so removing the
notice does not take the glyph along.

Robustness. The prpl may be missing or load after the plugin: its signals
are connected when `purple_plugins_find_with_id("prpl-xmpp2")` finds it
loaded, or on `plugin-load`, starting with `xmpp2-sent-message` (a prpl
without it gets no handlers, with an error in the debug log), and are
forgotten on `plugin-unload` (libpurple drops the handlers with the
instance). Unloading the plugin disconnects every signal and frees every
`X2TickBuf`; glyphs already drawn stay. Preferences under
`/plugins/gtk/xmpp2-ticks`: `enabled` (default on) and `style`
(`checks`, `ascii`, `words`).

Known limits: 1:1 only; no ticks on history, MAM or carbon lines, or after
the view was rebuilt; a contact that answers without the request caps
(7.4) gets none. Tests: `unit-tickmap` and `unit-tickbuf` (a plain
`GtkTextBuffer` fed like Pidgin writes, no display), `smoke-ticks-load`, and
`integration-ticks`, where a real Pidgin under Xvfb signs in to Prosody and
a test hook (`XMPP2_TEST_HOOKS`, `XMPP2_TICKS_TEST_PEER`) sends a line and
logs the view's text after each update.

## 8. Security rules (checklist for review and QA)

- Carbons: outer `from` must equal our bare JID exactly; `sent`: inner
  `from` bare JID must be ours; `received`: inner `to` bare JID must be ours.
- MAM results: outer `from` must be the queried archive (absent counts as our
  bare JID for the account archive only), `queryid` must match.
- MAM inner message, account archive: inner `from` or `to` bare JID must be
  ours. Room archive: inner `from` bare JID must equal the room JID, and the
  message is always delivered as groupchat into that room (never as a 1:1
  from a spoofed JID).
- Dedup keys: never derived from foreign `@id`; inner keys only from
  messages without a trusted stanza-id (7.3).
- stanza-id: trusted only when `by` matches the archive and the archive
  advertised support (per-stream flag, set from our account or room
  disco#info); otherwise ignored for dedup and cursors. Applies to live
  messages too.
- Service discovery: `server_info` only for our domain, our server's
  `disco#items` and our own bare JID; never for contacts or foreign
  domains (5).
- Corrections: sender must match the original sender (7.6).
- Receipts only to roster contacts with subscription, never for MAM/carbons.
- Upload: only the three allowed PUT headers, strip CR/LF from all header
  values, HTTPS GET URLs only unless the account allows plaintext.

## 9. Persistent state

Chosen: one GKeyFile per account at
`purple_user_dir()/xmpp2/<purple_escape_filename(purple_account_get_username())>.state`
(the full username including resource, so two accounts for the same bare JID
never share a file). The directory is created with
`purple_build_dir(dir, 0700)`. Written atomically with `purple_util_write_data_to_file_absolute()`
(temp file plus rename), debounced 2 s, flushed on disconnect and unload,
deleted on the `account-removed` signal. Prpls load before
`purple_accounts_init()` registers that signal, so `ext/mam.c` connects it
from a zero timeout armed in `plugin_init` (it runs once the main loop
does) or on the first stream, whichever comes first.

```
[archive user@example.org]
cursor=1a2b...           ; last delivered stanza-id
cursor_ts=1763540000     ; its timestamp, fallback for item-not-found
ring=O:out:bob@x:uuid1;O:in:carol@x:abc;...   ; last 256 non-S keys
[archive room@conference.example.org]
cursor=...
cursor_ts=...
[meta]
initial=cursor-only      ; set by migration
version=1
```

Why not `purple_account_set_string()`: room cursors are an unbounded,
dynamic key set; live MUC traffic updates cursors constantly; every account
setting change rewrites all of `accounts.xml` (which holds passwords) via
libpurple's 5 s save timer. A separate small file is cheap to write, safe to
delete (resets to the first-run rule), and testable in `core/store.c` with a
temp path. Account-level user choices (`mam_initial_days`,
`mam_muc_threshold_days`, notices) are normal account options.

## 10. Migration from prpl-jabber (M8, done)

`ext/migrate.c` (module, startup offer, action, the loop over the
originals), `ext/migrate_clone.c` (one account), `ext/migrate_blist.c`
(buddy list entries) and the pure `core/migrate_map.c` (option mapping,
username equality). Libpurple loads protocol plugins during
`purple_plugins_probe()`, before the accounts are loaded (`core.c`), so
`plugin_init` only arms a 2 s `purple_timeout_add_seconds()`; it fires in the
main loop with the accounts and, in Pidgin, the buddy list window present.

- Offer once, only when `purple_core_get_ui()` is `"gtk-gaim"` (Pidgin's
  historical UI id, `pidgin/pidgin.h` 44; Finch, the harness and other UIs
  never get an unsolicited dialog) and the pref
  `/plugins/prpl/xmpp2/migration_never` is false: if some `prpl-jabber`
  account has no `prpl-xmpp2` twin, `purple_request_action()` asks "Copy N
  XMPP accounts to XMPP (xmpp2)?" with Copy, Not now, Never. Never sets the
  pref, Not now does nothing (asks again next start). Test hook (only in
  `-DXMPP2_TEST_HOOKS=ON` builds): the pref
  `/plugins/prpl/xmpp2/migration_offer_any_ui` makes any UI get the offer.
- The same copy is available as the action "Copy XMPP accounts to
  xmpp2..." in two places. (a) The prpl action (`jabber_actions()`), in the
  menu of every connected xmpp2 account, every UI. Pidgin shows prpl actions
  only for a connected account of that prpl (`gtkblist.c` 8249), so after
  "Never", or with no xmpp2 account, it cannot be reached. (b) A plugin
  action under Tools > Plugin Actions > XMPP (xmpp2), which needs no
  account. Pidgin's Tools menu skips protocol plugins
  (`pidgin_blist_update_plugin_actions()`), so `plugin_init` registers a
  second, standard-type plugin with `purple_plugin_new()` and
  `purple_plugin_register()`: id `core-xmpp2-migrate`, flag
  `PURPLE_PLUGIN_FLAG_INVISIBLE` (not in the plugin list, not saved, cannot be
  unloaded by the user), no path, loaded at once and destroyed in
  `plugin_uninit` (its `PurplePluginInfo` lives in our module, so it must go
  before the module does). Both entries ask first (Copy, Cancel), tell the
  result with `purple_notify_info()`, and say so when there is nothing to
  copy.
- Clone: username (JID and resource), saved password (only if the original
  remembers it; never logged), alias, user info, buddy icon path, the
  account avatar (`purple_buddy_icons_find_account_icon(orig)` and
  `purple_buddy_icons_set_account_icon(clone, copy)`, so the clone publishes
  the same picture), check-mail flag, proxy info, and each setting of `x2_migrate_map_options()`: the
  options of the loaded prpl-jabber (`protocol_options`) that xmpp2 declares
  under the same key with the same kind (string or list, int, bool). A value
  a list option does not offer is skipped. libpurple cannot tell "unset" from
  "default", so a getter is called twice with two different defaults: equal
  answers mean the value is stored. Options that only one side has
  (`unknown_to_xmpp2`) are dropped.
- Buddy list (`migrate_blist.c`): groups are shared, every contact that has a
  buddy of the original gets a contact of the clone with the same group,
  local aliases (buddy and contact), and every `PurpleChat` (room bookmark)
  is copied with its components, alias and Pidgin's auto-join and persistent
  flags. Buddies are only added locally, nothing is sent: the roster is
  server side and refreshes at the first login. Not copied: buddy notes,
  pounces, contacts merged with buddies of other accounts.
- Enable: the original is always disabled (`purple_account_set_enabled(orig,
  ui, FALSE)`, which also signs it off); the clone is enabled afterwards only
  if the original was enabled (the same full JID twice would make the server
  kick one session with a resource conflict, in a loop). Never deleted.
  Idempotent: an account is skipped when a `prpl-xmpp2` account with the
  same bare JID exists (case-insensitive, the resource is ignored: it is only
  how the account logs in, and a second session with it would conflict), so
  two prpl-jabber accounts that differ only in the resource get one copy.
- State: `[meta] initial=cursor-only` is written to the clone's state file
  at creation (9), so the first catch-up only stores a cursor and shows no
  history (7.2).
- Logs: libpurple derives the log directory from the prpl's `list_icon()`
  (`log.c` 350) and the normalised username, which drops the resource. We
  return `"jabber"`, so the clone writes to the same `logs/jabber/<bare
  jid>/` and the history viewer shows one continuous history.

## 11. Icons and UI

`list_icon()` returns `"jabber"`: Pidgin resolves
`pixmaps/pidgin/protocols/<size>/jabber.png`, already installed, and the log
directory stays shared (10). No icon files to ship. Users who keep both
accounts tell them apart by the protocol name in the account list.

## 12. Testability

Test contract (fixed by the tech lead): `ctest` labels

| Label | Needs | Where it runs |
|---|---|---|
| `unit` | nothing (no network) | distro build containers (Debian 13, Ubuntu 24.04, Fedora) |
| `smoke` | pidgin + xvfb | CI host job |
| `integration` | podman Prosody | CI host job; exit 77 means skipped |

Ownership: `tests/unit/` belongs to the engineer of each feature; the rest
of `tests/` belongs to QA.

- Unit (`tests/unit/`, GLib GTest, label `unit`): every `core/` file, fed with
  XML strings through `xmlnode_from_str()`. Links libpurple for `xmlnode`
  only; no `purple_core_init()`. Covers forged carbons, stanza-id `by`
  checks, `<fin>` parsing, dedup LRU eviction, store round-trip and
  corruption handling, PUT request building and header sanitising.
  `test_msgctx.c` also runs the real `ext/ext.c` and `ext/msgctx.c` against
  a test module table (`X2_EXT_TEST_TABLE`) with stubbed delivery: stanza-id
  trust, delay precedence, `X2_DEFERRED` dup/redeliver ownership, priv
  slots. Criticals are fatal in unit tests; expected ones are announced with
  `g_test_expect_message()`.
- Memory checks: `cmake -DXMPP2_SANITIZE=ON` builds everything with ASan and
  UBSan; run `ctest -L unit` there (the sanitized plugin does not load into
  a normal Pidgin, so skip `smoke`). `valgrind --leak-check=full` on the
  `test-*` binaries works on a normal build too.
- Integration (`tests/integration/`): the existing `xmpp2-harness`
  (nullclient-style, line script) against Prosody 13 in podman, with
  `peer.py` (slixmpp) as the other party or as a second device of the same
  account. Needed harness extensions are listed in PLAN.md M1.
- Smoke (`tests/smoke/`): Pidgin under Xvfb loads both prpls.
- Every feature milestone ships its unit tests and at least one
  integration script; CI runs `unit` on Debian 13, Ubuntu 24.04, Fedora and
  `smoke` plus `integration` in the host job.
