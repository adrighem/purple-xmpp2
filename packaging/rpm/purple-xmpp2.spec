# x-release-please-start-version
Version:        0.2.0
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
BuildRequires:  pkgconfig(pidgin) >= 2.14
BuildRequires:  pkgconfig(gtk+-2.0)
BuildRequires:  libidn-devel
Recommends:     pidgin
Suggests:       (pidgin-xmpp2-ticks if pidgin)

%description
purple-xmpp2 is a third-party XMPP protocol plugin (prpl-xmpp2) for Pidgin
and other libpurple 2 clients. It is forked from the built-in jabber plugin
and adds XEP-0313 Message Archive Management so messages received while
offline or on other devices appear after connecting, plus message carbons,
stream management, HTTP file upload, receipts, chat markers, and message
correction. It installs next to the built-in XMPP plugin.

%package -n pidgin-xmpp2-ticks
Summary:        Delivery and read ticks for purple-xmpp2 in Pidgin
Requires:       pidgin >= 2.14
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description -n pidgin-xmpp2-ticks
pidgin-xmpp2-ticks is an optional Pidgin plugin for XMPP accounts that use
the purple-xmpp2 protocol plugin. It appends a tick to your sent messages in
the conversation window when the contact's client confirms delivery
(XEP-0184 receipt), and a second one when it reports the message as
displayed (XEP-0333 chat marker). Enable it under Tools > Plugins.

%prep
%autosetup

%build
%cmake -G Ninja -DBUILD_TESTING=OFF -DXMPP2_TEST_HOOKS=OFF \
       -DXMPP2_PIDGIN_PLUGIN=ON
%cmake_build

%install
%cmake_install

%files
%license LICENSE
%doc README.md COPYRIGHT.pidgin
%{_libdir}/purple-2/libxmpp2.so

%files -n pidgin-xmpp2-ticks
%license LICENSE
%{_libdir}/pidgin/pidgin-xmpp2-ticks.so
