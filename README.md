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
| Message archive catch-up | XEP-0313 | Planned (headline feature) |
| Message carbons | XEP-0280 | Planned |
| Stream management | XEP-0198 | Planned |
| HTTP file upload | XEP-0363 | Planned |
| Delivery receipts | XEP-0184 | Planned |
| Chat markers | XEP-0333 | Planned |
| Message correction | XEP-0308 | Planned |

Limits for v1: receipts and chat markers are exposed as protocol support,
signals, and an optional notice line, with no tick icons in the GTK UI. Stream
resumption is best effort and may slip to v0.2. Google Talk extensions, Jingle
(voice and video), BOSH, Cyrus SASL, and DIGEST-MD5 are dropped. Requires
libpurple 2.14.12 or newer.

On first connect, catch-up fetches the last day of the archive by default; this
is an account option (0 disables it). Cloned accounts behave the same, so a few
messages may appear twice next to the old XMPP plugin's logs.

This table is updated as each milestone lands; see
[docs/PLAN.md](docs/PLAN.md) for the schedule and
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the design.

## Install

### Debian 13 and Ubuntu 24.04

Download the `.deb` for your distribution from the
[GitHub Releases page](https://github.com/adrighem/purple-xmpp2/releases) (the
primary install path) and install it:

```sh
sudo apt install ./purple-xmpp2_<version>-1_<debian-13|ubuntu-24.04>_amd64.deb
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
(`pkg-config --variable=plugindir purple`). Fully quit and restart Pidgin, then
look for **XMPP2** under Accounts, Manage Accounts, Add.

## Using it

### Migrating an existing XMPP account

Pidgin's **Accounts, Manage Accounts** dialog lets you add an account with the
**XMPP2** protocol using the same username, domain, and password as your
existing XMPP account. If the clone option is available in your build, choose
it to copy an existing XMPP account. Keep the original until you are happy, then
disable or delete it. Do not enable both accounts at once: two sessions with the
same resource conflict, so give the new account a different resource.

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
