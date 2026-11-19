# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.16)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt"
     XMPP2_VERSION LIMIT_COUNT 1)
if(NOT XMPP2_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "version.txt must contain one semantic version")
endif()

project(purple-xmpp2 VERSION "${XMPP2_VERSION}" LANGUAGES C)

include(CTest)
include(GNUInstallDirs)
include(CheckSymbolExists)
find_package(PkgConfig REQUIRED)

option(XMPP2_WERROR "Treat compiler warnings as errors" OFF)
option(XMPP2_IDN "Use libidn for stringprep (recommended)" ON)
option(XMPP2_SANITIZE
    "Build with AddressSanitizer and UBSan (for ctest -L unit; the plugin then only loads into a sanitized host)"
    OFF)

if(XMPP2_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer
                        -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

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
    src/auth_plain.c
    src/auth_scram.c
    src/buddy.c
    src/caps.c
    src/chat.c
    src/data.c
    src/disco.c
    src/ext/carbons.c
    src/ext/ext.c
    src/ext/mam.c
    src/ext/mam_sync.c
    src/ext/markers.c
    src/ext/msgctx.c
    src/ext/statefile.c
    src/ext/upload.c
    src/ext/upload_xfer.c
    src/ext/peerutil.c
    src/ext/receipts.c
    src/ibb.c
    src/iq.c
    src/jabber.c
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

# Pure protocol logic (glib + xmlnode only), shared with the unit tests.
add_library(x2core STATIC
    src/core/carbon_parse.c
    src/core/dedup.c
    src/core/forward.c
    src/core/mam_proto.c
    src/core/receipt_parse.c
    src/core/sid.c
    src/core/smstate.c
    src/core/store.c
    src/core/upload_proto.c)
set_target_properties(x2core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden)
target_compile_features(x2core PRIVATE c_std_11)
target_compile_options(x2core PRIVATE -Wall -Wextra)
if(XMPP2_WERROR)
    target_compile_options(x2core PRIVATE -Werror)
endif()
target_include_directories(x2core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/core")
target_link_libraries(x2core PUBLIC PkgConfig::PURPLE PkgConfig::GLIB)

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
# Inherited callbacks have fixed signatures with many unused parameters, and
# xdata.c calls one callback type through another (works on every ABI we
# build for); keep both quiet until xdata.c is reworked.
target_compile_options(xmpp2 PRIVATE -Wall -Wextra
    -Wno-unused-parameter -Wno-cast-function-type)
if(XMPP2_WERROR)
    target_compile_options(xmpp2 PRIVATE -Werror)
endif()
target_include_directories(xmpp2 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(xmpp2 PRIVATE
    x2core PkgConfig::PURPLE PkgConfig::GLIB PkgConfig::LIBXML m)

# Keep our jabber_* symbols private: the built-in libjabber.so.0 may live in
# the same process.  -fvisibility=hidden plus the version script leave only
# purple_init_plugin exported; tests/unit_exports.sh verifies it.
set(XMPP2_VERSION_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/src/xmpp2.map")
target_link_options(xmpp2 PRIVATE
    "LINKER:--version-script=${XMPP2_VERSION_SCRIPT}"
    "LINKER:--no-undefined")
set_target_properties(xmpp2 PROPERTIES LINK_DEPENDS "${XMPP2_VERSION_SCRIPT}")

# gettext: our own domain, bound at plugin load (see libxmpp.c)
check_symbol_exists(dgettext libintl.h XMPP2_HAVE_GETTEXT)
if(XMPP2_HAVE_GETTEXT)
    target_compile_definitions(xmpp2 PRIVATE
        ENABLE_NLS
        XMPP2_LOCALEDIR="${CMAKE_INSTALL_FULL_LOCALEDIR}")
endif()

if(XMPP2_IDN)
    pkg_check_modules(LIBIDN REQUIRED IMPORTED_TARGET libidn)
    target_compile_definitions(xmpp2 PRIVATE USE_IDN)
    target_link_libraries(xmpp2 PRIVATE PkgConfig::LIBIDN)
endif()

install(TARGETS xmpp2 LIBRARY DESTINATION "${XMPP2_PLUGIN_DIR}")

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
