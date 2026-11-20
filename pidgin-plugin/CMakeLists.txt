# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 purple-xmpp2 contributors
#
# pidgin-xmpp2-ticks: optional Pidgin (GTK 2) plugin drawing delivery and
# read ticks from the prpl-xmpp2 signals.  Included from the top level when
# XMPP2_PIDGIN_PLUGIN found pidgin.pc; the prpl itself never links GTK.

# Tick logic without Pidgin, shared with the unit tests.
add_library(x2ticks STATIC tickmap.c tickbuf.c)
set_target_properties(x2ticks PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden)
target_compile_features(x2ticks PRIVATE c_std_11)
# GTK and GHRFunc callbacks have fixed signatures.
target_compile_options(x2ticks PRIVATE -Wall -Wextra ${XMPP2_FORMAT_WARNINGS}
    -Wno-unused-parameter)
if(XMPP2_WERROR)
    target_compile_options(x2ticks PRIVATE -Werror)
endif()
target_include_directories(x2ticks PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(x2ticks PUBLIC PkgConfig::GTK2)

add_library(pidgin-xmpp2-ticks MODULE ticks.c)
set_target_properties(pidgin-xmpp2-ticks PROPERTIES
    PREFIX ""
    OUTPUT_NAME pidgin-xmpp2-ticks
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
target_compile_features(pidgin-xmpp2-ticks PRIVATE c_std_11)
target_compile_definitions(pidgin-xmpp2-ticks PRIVATE
    PURPLE_PLUGINS
    XMPP2_VERSION="${PROJECT_VERSION}")
if(XMPP2_TEST_HOOKS)
    target_compile_definitions(pidgin-xmpp2-ticks PRIVATE XMPP2_TEST_HOOKS)
    target_sources(pidgin-xmpp2-ticks PRIVATE ticks_test.c)
endif()
# Signal callbacks have fixed signatures with unused parameters.
target_compile_options(pidgin-xmpp2-ticks PRIVATE -Wall -Wextra
    ${XMPP2_FORMAT_WARNINGS} -Wno-unused-parameter)
if(XMPP2_WERROR)
    target_compile_options(pidgin-xmpp2-ticks PRIVATE -Werror)
endif()
target_link_libraries(pidgin-xmpp2-ticks PRIVATE
    x2ticks PkgConfig::PIDGIN PkgConfig::GLIB)

# Same export rule as the prpl: only purple_init_plugin.  No --no-undefined:
# pidgin_* symbols come from the pidgin executable at load time.
target_link_options(pidgin-xmpp2-ticks PRIVATE
    "LINKER:--version-script=${XMPP2_VERSION_SCRIPT}")
set_target_properties(pidgin-xmpp2-ticks PROPERTIES
    LINK_DEPENDS "${XMPP2_VERSION_SCRIPT}")

install(TARGETS pidgin-xmpp2-ticks
    LIBRARY DESTINATION "${XMPP2_PIDGIN_PLUGIN_DIR}")
