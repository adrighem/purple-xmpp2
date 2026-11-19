# purple-xmpp2

> [!WARNING]
> **Early development (v0.1.0 candidate).** This is an unofficial third-party
> plugin and is not part of Pidgin.

`purple-xmpp2` is an XMPP protocol plugin (`prpl-xmpp2`, shown as **XMPP
(xmpp2)**) for Pidgin 2.14 and other libpurple 2 clients. It is forked from
Pidgin's built-in jabber plugin and adds the modern XMPP features the built-in
plugin lacks.

## Why

The built-in XMPP plugin only shows messages that arrive while you are online
and connected. Messages sent while Pidgin was closed, or delivered to your phone
or another client, never appear. purple-xmpp2 fixes that with **XEP-0313
Message Archive Management (MAM)**: on connect it fetches what you missed from
the server archive and shows it in the right conversation, in order, once.

## Features

| Feature | XEP | Status in v0.1.0 |
| --- | --- | --- |
| Everything the built-in XMPP plugin does, except the dropped parts below | | Inherited (forked from Pidgin 2.14.14) |
| Message archive catch-up | XEP-0313 | 1:1 chats and group chats. Offline and other-device messages appear on login with their original time; room history on join comes from the room archive instead of replayed legacy history |
| Message carbons | XEP-0280 | Messages you send or receive on other devices show up here |
| Stream management | XEP-0198 | Batched acks, no blind resend of unacked messages; unacked messages are reported when the connection drops. No resumption yet |
| HTTP file upload | XEP-0363 | Used for sending files; falls back to SI file transfer when the server has no upload service |
| Delivery receipts | XEP-0184 | Sent and received; optional notice line, signal `xmpp2-receipt` |
| Chat markers | XEP-0333 | 1:1 chats: `displayed` is sent when you focus the conversation; signal `xmpp2-marker` |
| Message correction | XEP-0308 | Corrections show as a new "corrected:" line; `/correct TEXT` fixes your last message |
| Account migration from the built-in plugin | | Copies accounts, buddy list and room bookmarks |

### How offline messages work

On connect, the plugin asks the server archive for everything newer than the
last message it saw, then shows those messages with their original timestamps.
One-to-one offline and delayed messages are flagged as delayed
(`PURPLE_MESSAGE_DELAYED`). Duplicates (a message seen live, via a carbon, and
via the archive) are removed using the server's stable ids.

A new XMPP2 account has no stored position, so its first sync fetches the last
`mam_initial_days` days (default 1, set 0 to fetch nothing and just record the
position). An account migrated from the built-in plugin does no history replay:
its first sync only records a position, so you get no duplicates next to the old
plugin's logs. Joining a room fetches only what you missed; if the stored
position is older than `mam_muc_threshold_days` (default 2) or missing, only the
latest page of the room archive is fetched. Large catch-ups are capped per
login, with a notice in the conversation.

Your server must support MAM (Prosody `mod_mam`, ejabberd `mod_mam`, and so on).
Without it the plugin behaves like the built-in one.

### Account options

Beyond the usual XMPP options (connection security, port, connect server, file
transfer proxies, custom smileys), the Advanced tab has:

| Option | Default | Meaning |
| --- | --- | --- |
| Days of history to fetch on first login | 1 | `mam_initial_days`; 0 means none |
| Days a room's history position stays valid | 2 | `mam_muc_threshold_days` |
| Show delivery and read notices | off | Prints "Message delivered." style lines in the conversation |
| Allow unencrypted file upload | off | `allow_plain_http_upload`; permits `http://` upload slots |

### Limitations

- No stream resumption. After a network drop Pidgin reconnects normally and MAM
  fills the gap (planned for v0.2).
- Receipts and markers have no tick icons in the GTK UI. They are shown only as
  the optional notice line and as signals for plugins.
- A corrected message appears as a new line, not an in-place edit.
- File upload needs a server with XEP-0363 over `https` (or the opt-in above).
  The whole file is read into memory, there is no progress bar, and the server's
  size limit is checked before sending.
- No OMEMO or other end-to-end encryption, no message retraction, reactions, or
  replies yet.
- Dropped from the fork: Google Talk extensions, Jingle (voice and video), BOSH,
  Cyrus SASL, and DIGEST-MD5. Use the built-in plugin if you need them.
- Requires libpurple 2.14.12 or newer.

See [docs/PLAN.md](docs/PLAN.md) for the roadmap,
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the design, and
[docs/GAJIM-COMPARISON.md](docs/GAJIM-COMPARISON.md) for the feature gap.

## Install

### Debian 13 and Ubuntu 24.04

Download the `.deb` for your distribution from the
[GitHub Releases page](https://github.com/adrighem/purple-xmpp2/releases) (the
primary install path) and install it:

```sh
sudo apt install ./purple-xmpp2_<version>_<debian-13|ubuntu-24.04>_amd64.deb
```

A signed APT repository on GitHub Pages is coming soon.

### Fedora

```sh
sudo dnf install ./purple-xmpp2-<version>-1.<dist>.x86_64.rpm
```

### From source

```sh
sudo apt install build-essential cmake pkgconf libpurple-dev libglib2.0-dev \
  libxml2-dev libidn-dev   # Fedora: see CONTRIBUTING.md
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

This installs `libxmpp2.so` into libpurple's plugin directory
(`pkg-config --variable=plugindir purple`). For a per-user install without
root, use `cmake -B build -DXMPP2_PLUGIN_DIR=$HOME/.purple/plugins` and
`cmake --install build`. Fully quit and restart Pidgin, then
look for **XMPP2** under Accounts, Manage Accounts, Add.

## Using it

### Migrating an existing XMPP account

In Pidgin, the plugin offers to copy your built-in XMPP accounts to XMPP (xmpp2)
when it loads (choose "Copy", "Not now", or "Never"). Other libpurple clients do
not get this offer. You can run it later in Pidgin from **Tools, Plugin
Actions, XMPP (xmpp2), Copy XMPP accounts to xmpp2...**, which works without
any xmpp2 account and after "Never". Other clients, and Pidgin once an xmpp2
account is online, also have it in that account's action menu. (Pidgin shows
an account's actions only while it is connected.)

The copy takes the account settings, buddy list entries and room bookmarks. The
originals are kept but disabled (a disabled original stays disabled in the
copy). An account counts as already copied when an xmpp2 account with the same
bare JID exists, whatever its resource. The copy replays no history, as described above. Passwords are copied
only if they are saved. You can also add an **XMPP (xmpp2)** account by hand with
the same username, domain, and password. Do not run both accounts at once: two
sessions with the same resource conflict, so give the new account a different
resource.

### Coexistence with built-in XMPP

The plugin id (`prpl-xmpp2`) and file name (`libxmpp2.so`) differ from the
built-in plugin (`prpl-jabber`), so both can be installed and enabled together.
Accounts, buddy lists, and logs are separate per protocol. Use one protocol per
XMPP account at a time.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Please also read the
[code of conduct](CODE_OF_CONDUCT.md), [security policy](SECURITY.md), and
[support notes](SUPPORT.md).

## License

GPL-2.0-or-later. See [LICENSE](LICENSE). The code is derived from Pidgin's
libpurple jabber plugin, copyright its respective authors. Pidgin and libpurple
are projects of the Pidgin developers; purple-xmpp2 is not affiliated with them.
