# x-release-please-start-version
Version:        0.1.0
# x-release-please-end
Name:           purple-xmpp2
Release:        1%{?dist}
Summary:        XMPP protocol plugin with message archive catch-up for libpurple 2
License:        GPL-2.0-or-later
URL:            https://github.com/adrighem/purple-xmpp2
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.16
BuildRequires:  gcc
BuildRequires:  ninja-build
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(libxml-2.0)
BuildRequires:  pkgconfig(purple) >= 2.14
BuildRequires:  libidn-devel
Recommends:     pidgin

%description
purple-xmpp2 is a third-party XMPP protocol plugin (prpl-xmpp2) for Pidgin
and other libpurple 2 clients. It is forked from the built-in jabber plugin
and adds XEP-0313 Message Archive Management so messages received while
offline or on other devices appear after connecting, plus message carbons,
stream management, HTTP file upload, receipts, chat markers, and message
correction. It installs next to the built-in XMPP plugin.

%prep
%autosetup

%build
%cmake -G Ninja -DBUILD_TESTING=OFF
%cmake_build

%install
%cmake_install

%files
%license LICENSE
%doc README.md
%{_libdir}/purple-2/libxmpp2.so

%changelog
* Thu Nov 19 2026 Vincent van Adrighem <vincent-git@vanadrighem.eu> - 0.1.0-1
- Initial package
