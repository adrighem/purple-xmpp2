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
# Debian / Ubuntu (pidgin-dev only for the optional ticks plugin)
sudo apt install build-essential cmake ninja-build pkgconf libpurple-dev \
  libglib2.0-dev libxml2-dev libidn-dev pidgin-dev
# Fedora
sudo dnf install cmake ninja-build gcc pkgconf-pkg-config libpurple-devel \
  glib2-devel libxml2-devel libidn-devel pidgin-devel

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

To install per user instead, configure with
`-DXMPP2_PLUGIN_DIR=$HOME/.purple/plugins` and run `cmake --install build`.
To try the plugin without touching the system, copy `build/libxmpp2.so` to
`~/.purple/plugins/` and start `pidgin`. The plugin id is `prpl-xmpp2`; it
coexists with the built-in XMPP plugin.

The optional Pidgin plugin `pidgin-xmpp2-ticks` (sources in `pidgin-plugin/`,
design in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) 7.9) is built when
`pkg-config` finds `pidgin.pc` (`-DXMPP2_PIDGIN_PLUGIN=AUTO`, the default;
`ON` makes Pidgin and GTK 2 required, `OFF` skips it). It installs into
`pkg-config --variable=plugindir pidgin`; to try it, copy
`build/pidgin-plugin/pidgin-xmpp2-ticks.so` to `~/.purple/plugins/` and
enable **XMPP2 Delivery Ticks** under Tools, Plugins. Only this plugin links
GTK; keep `src/` free of GTK and Pidgin headers.

## Test

```sh
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON -DXMPP2_TEST_HOOKS=ON
cmake --build build
ctest --test-dir build -L unit           # fast, no external tools
ctest --test-dir build -L smoke          # Pidgin under Xvfb
ctest --test-dir build -L integration    # needs podman and python3-venv
```

Tests exit 77 (skipped) when a prerequisite such as podman, Xvfb, or Pidgin is
missing. Layers, fixtures, and the Prosody test server are described in
[tests/README.md](tests/README.md). Add or update tests for behavior changes.

CMake options for development:

- `XMPP2_TEST_HOOKS=ON` compiles test-only hooks (environment switches such as
  `XMPP2_TEST_MAM_STALL`) that some integration tests need. CI uses it. Never
  use it for release or distro packages; the package builds leave it off.
- `XMPP2_SANITIZE=ON` builds with AddressSanitizer and UBSan. Use it for
  `ctest -L unit`; the sanitized plugin only loads into a sanitized host.
- `XMPP2_WERROR=ON` turns warnings into errors.
- `XMPP2_PIDGIN_PLUGIN=AUTO|ON|OFF` controls the ticks plugin (see Build).
  Its unit tests (`unit-tickmap`, `unit-tickbuf`) need no display; the smoke
  test `smoke-ticks-load` and `integration-ticks` run a real Pidgin under Xvfb.

Several suites on one host restart each other's test server. Give each its own:
`XMPP2_TEST_CONTAINER=name XMPP2_TEST_C2S_PORT=N XMPP2_TEST_HTTP_PORT=M`, plus
`XMPP2_SERVER_PROFILE=full|nocarbons|nomam|noupload` to switch server features
and `XMPP2_KEEP_SERVER=1` to leave the server running. Never put real JIDs,
passwords, or message contents in tests, fixtures, logs, or issues.

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
libpurple's plugin directory (`pkg-config --variable=plugindir purple`) and
`pidgin-xmpp2-ticks.so` in Pidgin's (`pkg-config --variable=plugindir
pidgin`). Each build produces two binary packages: `purple-xmpp2` (no GTK
dependency) and `pidgin-xmpp2-ticks` (depends on `pidgin` and the exact
`purple-xmpp2` version). Debian packaging lives in `debian/`, the RPM spec
(main package plus the `pidgin-xmpp2-ticks` subpackage) in
`packaging/rpm/`.

By contributing, you agree that your contributions are provided under
GPL-2.0-or-later.
