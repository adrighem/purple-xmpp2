# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.16)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt"
     XMPP2_VERSION LIMIT_COUNT 1)
if(NOT XMPP2_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "version.txt must contain one semantic version")
endif()

project(purple-xmpp2 VERSION "${XMPP2_VERSION}" LANGUAGES C)

include(CTest)
find_package(PkgConfig REQUIRED)

option(XMPP2_WERROR "Treat compiler warnings as errors" OFF)
option(XMPP2_IDN "Use libidn for stringprep (recommended)" ON)
option(XMPP2_CYRUS_SASL "Use Cyrus SASL for authentication" OFF)

pkg_check_modules(PURPLE REQUIRED IMPORTED_TARGET purple>=2.14)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0 gobject-2.0 gmodule-2.0)
pkg_check_modules(LIBXML REQUIRED IMPORTED_TARGET libxml-2.0)

# Plugin directory as reported by libpurple, unless overridden.
pkg_get_variable(PURPLE_PLUGINDIR purple plugindir)
if(NOT PURPLE_PLUGINDIR)
    set(PURPLE_PLUGINDIR "lib/purple-2")
endif()
set(XMPP2_PLUGIN_DIR "${PURPLE_PLUGINDIR}"
    CACHE PATH "libpurple 2 protocol plugin installation directory")

set(XMPP2_SOURCES
    src/adhoccommands.c
    src/auth.c
    src/auth_digest_md5.c
    src/auth_plain.c
    src/auth_scram.c
    src/bosh.c
    src/buddy.c
    src/caps.c
    src/chat.c
    src/data.c
    src/disco.c
    src/google/gmail.c
    src/google/google.c
    src/google/google_presence.c
    src/google/google_roster.c
    src/google/google_session.c
    src/google/jingleinfo.c
    src/google/relay.c
    src/ibb.c
    src/iq.c
    src/jabber.c
    src/jingle/content.c
    src/jingle/iceudp.c
    src/jingle/jingle.c
    src/jingle/rawudp.c
    src/jingle/rtp.c
    src/jingle/session.c
    src/jingle/transport.c
    src/jutil.c
    src/libxmpp.c
    src/message.c
    src/oob.c
    src/parser.c
    src/pep.c
    src/ping.c
    src/presence.c
    src/roster.c
    src/si.c
    src/stream_management.c
    src/useravatar.c
    src/usermood.c
    src/usernick.c
    src/usertune.c
    src/xdata.c)

add_library(xmpp2 MODULE ${XMPP2_SOURCES})
# Install as libxmpp2.so, no "lib" prefix doubling.
set_target_properties(xmpp2 PROPERTIES
    OUTPUT_NAME xmpp2
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

target_compile_features(xmpp2 PRIVATE c_std_11)
target_compile_definitions(xmpp2 PRIVATE
    PURPLE_PLUGINS
    XMPP2_VERSION="${PROJECT_VERSION}")
# Upstream callbacks have fixed signatures with many unused parameters, and the
# Jingle GObject classes use pre-2.58 GLib idioms (class_init casts and
# g_type_class_add_private); keep those three quiet until the code is reworked.
target_compile_options(xmpp2 PRIVATE -Wall -Wextra
    -Wno-unused-parameter -Wno-cast-function-type -Wno-deprecated-declarations)
if(XMPP2_WERROR)
    target_compile_options(xmpp2 PRIVATE -Werror)
endif()
target_include_directories(xmpp2 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(xmpp2 PRIVATE
    PkgConfig::PURPLE PkgConfig::GLIB PkgConfig::LIBXML m)

# Keep our jabber_* symbols private: the built-in libjabber.so.0 may live in
# the same process.  Only purple_init_plugin is exported (G_MODULE_EXPORT).
target_link_options(xmpp2 PRIVATE
    "LINKER:-Bsymbolic" "LINKER:--no-undefined")

if(XMPP2_IDN)
    pkg_check_modules(LIBIDN REQUIRED IMPORTED_TARGET libidn)
    target_compile_definitions(xmpp2 PRIVATE USE_IDN)
    target_link_libraries(xmpp2 PRIVATE PkgConfig::LIBIDN)
endif()

if(XMPP2_CYRUS_SASL)
    pkg_check_modules(LIBSASL REQUIRED IMPORTED_TARGET libsasl2)
    target_sources(xmpp2 PRIVATE src/auth_cyrus.c)
    target_compile_definitions(xmpp2 PRIVATE HAVE_CYRUS_SASL)
    target_link_libraries(xmpp2 PRIVATE PkgConfig::LIBSASL)
endif()

install(TARGETS xmpp2 LIBRARY DESTINATION "${XMPP2_PLUGIN_DIR}")
