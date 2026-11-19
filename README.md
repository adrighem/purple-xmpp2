# purple-xmpp2

> [!WARNING]
> **Early development.** Nothing below marked "planned" works yet. This is an
> unofficial third-party plugin and is not part of Pidgin.

`purple-xmpp2` is an XMPP protocol plugin (`prpl-xmpp2`) for Pidgin 2.14 and
other libpurple 2 clients. It is forked from Pidgin's built-in jabber plugin
and adds the modern XMPP features the built-in plugin lacks.

## Why

The built-in XMPP plugin only shows messages that arrive while you are online
and connected. Messages sent while Pidgin was closed, or delivered to your phone
or another client, never appear. purple-xmpp2 fixes that with **XEP-0313
Message Archive Management (MAM)**: on connect it fetches what you missed from
the server archive and shows it in the right conversation.

## Features

| Feature | XEP | Status |
| --- | --- | --- |
| Everything the built-in XMPP plugin does | | Inherited (forked from Pidgin 2.14.14) |
| Message archive catch-up | XEP-0313 | Implemented for 1:1 (M3): offline and other-device messages on login, original times, in order, once; rooms planned (M4) |
| Message carbons | XEP-0280 | Implemented (M2) |
| Stream management | XEP-0198 | Batched acks; unacked messages reported on connection loss (resumption planned) |
| HTTP file upload | XEP-0363 | Implemented (M7), falls back to SI file transfer |
| Delivery receipts | XEP-0184 | Implemented (M5), signal `xmpp2-receipt` and optional notice |
| Chat markers | XEP-0333 | Implemented (M5), `displayed` sent on focus, signal `xmpp2-marker` |
| Message correction | XEP-0308 | Implemented (M6): corrections shown as a new "corrected:" line, `/correct TEXT` to fix your last message |

Limits for v1: receipts and chat markers are exposed as protocol support,
signals (`xmpp2-receipt`, `xmpp2-marker`), and an optional notice line (account
option "Show delivery and read notices", default off), with no tick icons in the GTK UI. Stream
resumption is best effort and may slip to v0.2. Google Talk extensions, Jingle
(voice and video), BOSH, Cyrus SASL, and DIGEST-MD5 are dropped. Requires
libpurple 2.14.12 or newer.

On first connect, a new XMPP2 account fetches the last day of the archive by
default (an account option; 0 disables it). An account cloned from an existing
XMPP account does no history replay: its first sync only records a cursor, so
you do not get duplicates next to the old plugin's logs.

One-to-one offline and delayed messages are flagged as delayed
(`PURPLE_MESSAGE_DELAYED`) and show their original timestamp.

Release is gated: v0.1.0 is not published until all v1 milestones pass.

This table is updated as each milestone lands; see
[docs/PLAN.md](docs/PLAN.md) for the schedule and
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the design.

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

In Pidgin, the plugin offers to migrate (clone) an existing XMPP account when
you first enable it; other libpurple clients do not get this offer. You can
always add an **XMPP2** account by hand with the same username, domain, and
password. A cloned account starts from the current point in the archive and
replays no history. Keep the original until you are happy, then disable or
delete it. Do not enable both accounts at once: two sessions with the same
resource conflict, so give the new account a different resource.

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
