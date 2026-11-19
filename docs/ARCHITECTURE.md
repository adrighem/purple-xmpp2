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
    mam.c               XEP-0313 sync state machines (account and rooms)
    statefile.c         persistence glue: path, debounce, account-removed
    receipts.c          XEP-0184
    markers.c           XEP-0333
    correction.c        XEP-0308
    upload.c            XEP-0363 (PurpleXfer glue)
    migrate.c           clone prpl-jabber accounts
  core/                 NEW  pure logic: glib + xmlnode.h only, no purple core,
                             no connection, no UI. Unit tested.
    sid.c               XEP-0359 stanza-id (with by check) / origin-id
    forward.c           XEP-0297 unwrap, XEP-0203 delay parse
    carbon_parse.c      classify carbon, reject forged
    mam_proto.c         build query (form + RSM), parse <result>, <fin>
    dedup.c             bounded LRU set of message keys
    smstate.c           XEP-0198 counters (mod 2^32), bounded unacked queue
    store.c             GKeyFile archive state: cursors and key rings
    receipt_parse.c     receipts and markers element parse/build
    replace_parse.c     correction element parse/build
    upload_proto.c      slot request/response, HTTP PUT request, status line
tests/
  unit/                 NEW  GTest per core/ file, run by ctest
  integration/          EXISTING harness.c, peer.py, test_*.sh
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
    void (*chat_joined)(JabberStream *js, JabberChat *chat);
} X2Ext;
```

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
4. `receipts`, `markers`, `correction`: consume pure control messages
   (`<received/>`, `<displayed/>` without body), answer receipt requests,
   rewrite corrections.
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
`JabberStream` is freed.

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
  line. This mirrors classic MUC history but without replaying it on every
  rejoin.
- Room without MAM: unchanged upstream behaviour (legacy history).

Ordering: Pidgin can only append to a conversation. While an archive is
`SYNCING`, live and carbon messages for it are queued and flushed in arrival
order after the last page, dedup applied at flush time. This also absorbs
the XEP-0160 offline flood that Prosody delivers right after initial
presence, because the account archive enters `SYNCING` in `pre_presence`.

Cursor updates: after every `<fin>` (cursor = `last`, timestamp = last
delivered `ts`), and for live messages only once the archive is `LIVE`
(Gajim's `is_catch_up_finished` rule). Never from a message that lacks a
trusted `stanza-id`.

Delivery guarantee: at least once across a crash, bounded by the 2 s write
debounce (see 9); exactly once otherwise.

### 7.3 Dedup keys

- `S:<stanza-id>` when the message has a trusted stanza-id (XEP-0359:
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
carbons after the last received stanza-id).

### 7.4 Delivery receipts (XEP-0184)

Outgoing: `<request/>` on 1:1 messages with an id. Incoming request: reply
`<received id=.../>` to the full JID only when `src=LIVE`, `type!='groupchat'`
and the sender is in the roster with presence subscription (no presence
leak to strangers; same as Gajim). Incoming `<received/>`: emit the purple
signal `xmpp2-receipt` (account, peer, id). Pidgin 2 cannot decorate an
already written line, so v1 has no visual ticks; an account option
"Show delivery and read notices" (default off) writes a short
`PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_NO_LOG` line. A GTK companion plugin
can render ticks from the signals later.

### 7.5 Chat markers (XEP-0333)

Outgoing messages get `<markable/>`. Sending `displayed`: connect to
`conversation-updated` with `PURPLE_CONV_UPDATE_UNSEEN`; Pidgin sets the
conversation data `unseen-count` to 0 when the user focuses the window
(`pidgin/gtkconv.c` 7860, 8491). At that moment send one `<displayed id>` for
the newest markable incoming message of that 1:1 conversation (including
ones fetched by MAM). 1:1 only in v1. Incoming markers: signal `xmpp2-marker`
plus the same optional notice. Read-state sync from our other devices
(sent `displayed` carbons, XEP-0490) is later.

### 7.6 Message correction (XEP-0308)

Incoming `<replace id=X>`: accept only if X was seen in this conversation
from the same sender (1:1: same bare JID; MUC: same occupant-id when
XEP-0421 is present, else same nick and X seen this session). Accepted
corrections are shown as a new line prefixed with a marker ("corrected:")
since libpurple cannot edit history; rejected ones are shown as plain new
messages without the marker. Outgoing: `/correct <text>` command replaces our
last message in that conversation (we keep its id per conversation).
Advertise `urn:xmpp:message-correct:0`.

### 7.7 HTTP File Upload (XEP-0363)

Implemented in `core/upload_proto.c` (pure logic, unit tested),
`ext/upload.c` (discovery, prpl entry points) and `ext/upload_xfer.c` (the
`PurpleXfer`).

Discovery: in `server_info`, a JID that is our server domain or an item of
our own server's `disco#items` (never our own bare JID, foreign upload
services are never offered) advertising `urn:xmpp:http:upload:0`, with
`max-file-size` from its x-data. The first service wins.

`send_file`, `new_xfer` and `chat_send_file` (rooms) create a `PurpleXfer`
(type SEND) with our own ops when a service exists, else they fall back to
the inherited SI transfer (`can_receive_file` is TRUE whenever a service
exists: the URL reaches anybody). On init the size is checked against the
smaller of `max-file-size` and the hard 100 MiB cap, before any network
request, and an empty file is refused. Then:

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
3. `PUT` through `purple_util_fetch_url_request_data_len_with_account()` with
   a hand-built binary-safe request (`Host`, `User-Agent`, `Content-Type`,
   `Content-Length`, slot headers, `Connection: close`): honours the account
   proxy, https through the purple SSL plugin (default port 443, checked),
   `include_headers=TRUE` to read the status line; any 2xx is success.
4. Send `<message type='chat|groupchat'>` with the GET URL as body plus
   `<x xmlns='jabber:x:oob'><url/></x>` through the normal outbound path
   (`x2_msg_prepare_outgoing`: origin-id, hooks), echo it locally for 1:1
   (a room reflects it), complete the xfer.

Errors reach the user through `purple_xfer_error()` ("File upload of X
failed: reason") and cancel the transfer; the same text is logged under
`xmpp2-upload`. Logging: never the slot URLs (signed tokens) or headers; the
only URL data in our log is the PUT host and scheme. libpurple itself logs
the fetched URL at debug level under `util`, which we cannot suppress with
this API.

Memory: v1 reads the file into one buffer (request head plus body), bounded
by `max-file-size` and the 100 MiB cap, and libpurple may hold a copy of the
request while it sends it, so peak use is up to about twice the file size.
The transfer is blocking only for the file read; the PUT is asynchronous.
Streaming from disk and progress are later. In-flight uploads live in the
module's per-stream state (not in `js->file_transfers`, whose entries the SI
code casts to its own struct) and are cancelled when the stream closes.
Receiving needs nothing new: `jm_body_with_oob()` already shows the URL.

### 7.8 Stream management (XEP-0198), audit result

Upstream `stream_management.c` implements counting and acks only. Findings:

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

M9 (done) fixes 2 to 5, contained in `stream_management.c` plus the pure
`core/smstate.c`:

- State (counters, unacked queue, `<enabled id max resume location>`) lives
  in a table keyed by `PurpleAccount`, not in `JabberStream`, so M10 can
  keep it across a transport reconnect. `jabber_close()` frees it via
  `jabber_sm_close()`; `JabberStream` keeps only `sm_state`.
- `<enable resume='true'/>` is sent and `<enabled/>` parsed and logged
  (debug `xmpp2-sm`), but never used for `<resume/>` until M10.
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
  wanted. A clean sign-off reports nothing.

Resumption (M10) is limited by Pidgin itself: on a NetworkManager "down"
event Pidgin calls `purple_account_disconnect()` on every account
(`pidgin/gtkconn.c` 187), and any `purple_connection_error` tears down
buddies and chats, which the server will not re-send after a resume. So
resumption only makes sense as a silent reconnect inside one
`PurpleConnection`: on socket error with a resumable session, keep the
connection in `PURPLE_CONNECTED`, reconnect TCP/TLS/SASL in the background
with backoff, send `<resume previd h>`, flush the remainder. On `<failed>`
or timeout, fall back to the normal `purple_connection_error` path where MAM
covers the gap. Requires splitting transport state from session state in
`JabberStream`; done last because it touches the most of `jabber.c`.

MAM, not SM, is the primary offline-messaging mechanism.

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
deleted on the `account-removed` signal.

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

## 10. Migration from prpl-jabber

`ext/migrate.c`. Libpurple loads protocol plugins during
`purple_plugins_probe()`, before `purple_accounts_init()` (`core.c` 149 vs
163), so the check is deferred with `purple_timeout_add(0)`.

- Offer once, only when `purple_core_get_ui()` is `"gtk-gaim"` (Pidgin's
  historical UI id, `pidgin/pidgin.h` 44; Finch,
  the harness and other UIs never get an unsolicited dialog): if some
  `prpl-jabber` account has no `prpl-xmpp2` twin (same username) and the pref
  `/plugins/prpl/xmpp2/migration_offered` is false,
  `purple_request_action()`: "Copy N XMPP accounts to XMPP (xmpp2)? The
  originals are kept but disabled." Buttons: Copy, Not now, Never.
- The same function is always available, in every UI, as the prpl action
  "Copy XMPP accounts..." on any connected xmpp2 account.
- Copy: username (JID/resource), saved password, alias, user info, buddy
  icon, proxy info, and each protocol option that prpl-jabber declares
  (`prpl_info->protocol_options` of the loaded prpl-jabber gives setting
  name and type) and that we also declare. Copy `PurpleChat` blist nodes
  (room bookmarks) with their components into the same group. The roster is
  server side and needs no copy. Not copied: buddy notes, pounces, local
  contact merges (documented).
- If the original is enabled: enable the clone, disable the original (same
  full JID on both would make the server kick one with a resource conflict,
  in a loop). If the original is disabled, the clone stays disabled too.
  Never delete. Idempotent: skips accounts that already have a twin.
- Seed the state file with `initial=cursor-only`, so the first catch-up does
  not re-show history prpl-jabber already logged (7.2).
- Logs: libpurple derives the log directory from `list_icon()`
  (`log.c` 350). We return `"jabber"`, so the clone writes to the same
  `logs/jabber/<user>/` and the history viewer shows one continuous history.

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
