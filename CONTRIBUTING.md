# Contributing

Contributions are welcome: focused fixes, tests, server compatibility reports,
and documentation. Open an issue before large protocol or architecture changes.
See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and [docs/PLAN.md](docs/PLAN.md)
for the design and the milestone plan.

## Build

Dependencies: CMake, a C compiler, Ninja (optional), pkg-config, and development
files for libpurple 2.14, GLib, libxml2, and libidn. Google Talk extensions,
Jingle, BOSH, Cyrus SASL, and DIGEST-MD5 were removed from
the fork and are not built.

```sh
# Debian / Ubuntu
sudo apt install build-essential cmake ninja-build pkgconf libpurple-dev \
  libglib2.0-dev libxml2-dev libidn-dev
# Fedora
sudo dnf install cmake ninja-build gcc pkgconf-pkg-config libpurple-devel \
  glib2-devel libxml2-devel libidn-devel

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

To install per user instead, configure with
`-DXMPP2_PLUGIN_DIR=$HOME/.purple/plugins` and run `cmake --install build`.
To try the plugin without touching the system, copy `build/libxmpp2.so` to
`~/.purple/plugins/` and start `pidgin`. The plugin id is `prpl-xmpp2`; it
coexists with the built-in XMPP plugin.

## Test

```sh
ctest --test-dir build --output-on-failure
```

Test layers, fixtures, and how to run the XMPP test server are described in
[tests/README.md](tests/README.md). Add or update tests for behavior changes.
Never put real JIDs, passwords, or message contents in tests, fixtures, logs, or
issues.

## Keeping the upstream diff clean

`src/` is a fork of Pidgin 2.14.14 `libpurple/protocols/jabber`. Commit
`89d4711` is the pristine, unmodified import. Rules that keep merging future
upstream fixes feasible:

- Prefer new files (for example `mam.c`) over edits to upstream files. Hook
  into upstream code with small, clearly marked changes.
- Do not reformat, rename, or reorder upstream code. No whitespace-only churn.
- Keep upstream-touching changes in their own commits, separate from new
  feature code, so `git diff 89d4711 -- src/` stays readable.
- Check what you changed relative to upstream with
  `git diff 89d4711 HEAD --stat -- src/`.

## Commits and pull requests

Use [Conventional Commits](https://www.conventionalcommits.org/):
`feat: add MAM catch-up on connect`, `fix: handle empty RSM set`,
`docs: clarify install`. Use `!` and a `BREAKING CHANGE:` footer only for an
intentional compatibility break. Only `feat` and `fix` appear in release notes.
Reference issues in the message (`refs #12`, `fixes #12`) so they close on
release.

Release Please owns `CHANGELOG.md`, `version.txt`,
`.release-please-manifest.json`, and the version in
`packaging/rpm/purple-xmpp2.spec`. Do not edit them in an ordinary pull request.

## Packaging and releases

CI builds on Debian 13, Ubuntu 24.04, and Fedora (latest), and builds `.deb` and
`.rpm` packages as workflow artifacts. Merging a release pull request makes
Release Please tag a release; the workflow then attaches the packages and a
source tarball, publishes the release, and updates the signed APT repository on
GitHub Pages. The packaging assumes `cmake --install` puts `libxmpp2.so` in
libpurple's plugin directory (`pkg-config --variable=plugindir purple`). Debian
packaging lives in `debian/`, the RPM spec in `packaging/rpm/`.

By contributing, you agree that your contributions are provided under
GPL-2.0-or-later.
