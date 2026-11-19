# Test wiring. Included from the top level with: if(BUILD_TESTING) add_subdirectory(tests)
# Labels: unit (no server), smoke (pidgin + xvfb-run), integration (podman).
# Exit code 77 means "skipped" for tests that need missing tools.

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB_T REQUIRED IMPORTED_TARGET glib-2.0)

add_executable(xmpp2-harness integration/harness.c)
target_link_libraries(xmpp2-harness PRIVATE PkgConfig::PURPLE PkgConfig::GLIB_T)
target_compile_options(xmpp2-harness PRIVATE -Wall -Wextra -Wno-unused-parameter)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/unit/CMakeLists.txt")
    add_subdirectory(unit)
endif()

# The plugin must export exactly purple_init_plugin.
add_test(NAME unit-exports
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/unit_exports.sh $<TARGET_FILE:xmpp2>)
set_tests_properties(unit-exports PROPERTIES LABELS unit)

find_program(XMPP2_PIDGIN pidgin)
find_program(XMPP2_XVFB xvfb-run)
add_test(NAME smoke-pidgin-load
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/smoke/pidgin-load.sh $<TARGET_FILE:xmpp2>)
set_tests_properties(smoke-pidgin-load PROPERTIES
    LABELS smoke SKIP_RETURN_CODE 77
    ENVIRONMENT "XMPP2_HARNESS=$<TARGET_FILE:xmpp2-harness>")
if(NOT XMPP2_PIDGIN OR NOT XMPP2_XVFB)
    set_tests_properties(smoke-pidgin-load PROPERTIES DISABLED TRUE)
endif()

find_program(XMPP2_PODMAN podman)
add_test(NAME integration-basic
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_if_podman.sh
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_basic.sh)
set_tests_properties(integration-basic PROPERTIES
    LABELS integration SKIP_RETURN_CODE 77 RUN_SERIAL TRUE TIMEOUT 180
    ENVIRONMENT "PRPL=prpl-xmpp2;PLUGIN_DIR=$<TARGET_FILE_DIR:xmpp2>;XMPP2_HARNESS=$<TARGET_FILE:xmpp2-harness>")

add_test(NAME integration-selftest
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_if_podman.sh
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_selftest.sh)
set_tests_properties(integration-selftest PROPERTIES
    LABELS integration SKIP_RETURN_CODE 77 RUN_SERIAL TRUE TIMEOUT 240
    ENVIRONMENT "PRPL=prpl-xmpp2;PLUGIN_DIR=$<TARGET_FILE_DIR:xmpp2>;XMPP2_HARNESS=$<TARGET_FILE:xmpp2-harness>")

# Feature integration tests: one test_<name>.sh per entry.
foreach(feature_test carbons carbons_off mam mam_bulk mam_recovery sm upload
        upload_fallback)
    add_test(NAME integration-${feature_test}
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_if_podman.sh
            ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_${feature_test}.sh)
    set_tests_properties(integration-${feature_test} PROPERTIES
        LABELS integration SKIP_RETURN_CODE 77 RUN_SERIAL TRUE TIMEOUT 240
        ENVIRONMENT "PRPL=prpl-xmpp2;PLUGIN_DIR=$<TARGET_FILE_DIR:xmpp2>;XMPP2_HARNESS=$<TARGET_FILE:xmpp2-harness>")
endforeach()
