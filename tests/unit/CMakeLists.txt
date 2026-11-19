# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 purple-xmpp2 contributors
#
# GTest unit tests (no libpurple core, no network), label "unit".
# Add a test: tests/unit/test_<name>.c and one x2_unit_test() line below;
# extra arguments are additional sources compiled into the test.

function(x2_unit_test name)
    add_executable(test-${name} test_${name}.c ${ARGN})
    target_link_libraries(test-${name} PRIVATE x2core)
    target_include_directories(test-${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(test-${name} PRIVATE c_std_11)
    target_compile_options(test-${name} PRIVATE -Wall -Wextra)
    if(XMPP2_WERROR)
        target_compile_options(test-${name} PRIVATE -Werror)
    endif()
    add_test(NAME unit-${name} COMMAND test-${name})
    set_tests_properties(unit-${name} PROPERTIES LABELS unit TIMEOUT 10)
endfunction()

x2_unit_test(sid)
x2_unit_test(forward)
x2_unit_test(dedup)
x2_unit_test(carbon_parse)
x2_unit_test(mam_proto)
x2_unit_test(smstate)
x2_unit_test(store)
x2_unit_test(upload)
x2_unit_test(receipt_parse)

# The X2Msg pipeline: real ext.c, msgctx.c and jutil.c against the module
# table and stubs in test_msgctx.c.
x2_unit_test(msgctx
    ${PROJECT_SOURCE_DIR}/src/ext/ext.c
    ${PROJECT_SOURCE_DIR}/src/ext/msgctx.c
    ${PROJECT_SOURCE_DIR}/src/jutil.c)
target_include_directories(test-msgctx PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(test-msgctx PRIVATE
    PURPLE_PLUGINS X2_EXT_TEST_TABLE=x2_test_exts)
target_compile_options(test-msgctx PRIVATE -Wno-unused-parameter)
target_link_libraries(test-msgctx PRIVATE PkgConfig::LIBXML)
if(XMPP2_IDN)
    target_compile_definitions(test-msgctx PRIVATE USE_IDN)
    target_link_libraries(test-msgctx PRIVATE PkgConfig::LIBIDN)
endif()

# The carbons module inside the real pipeline, with stubbed IQ and delivery.
x2_unit_test(carbons
    ${PROJECT_SOURCE_DIR}/src/ext/carbons.c
    ${PROJECT_SOURCE_DIR}/src/ext/ext.c
    ${PROJECT_SOURCE_DIR}/src/ext/msgctx.c
    ${PROJECT_SOURCE_DIR}/src/jutil.c)
target_include_directories(test-carbons PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(test-carbons PRIVATE
    PURPLE_PLUGINS X2_EXT_TEST_TABLE=x2_test_exts)
target_compile_options(test-carbons PRIVATE -Wno-unused-parameter)
target_link_libraries(test-carbons PRIVATE PkgConfig::LIBXML)
if(XMPP2_IDN)
    target_compile_definitions(test-carbons PRIVATE USE_IDN)
    target_link_libraries(test-carbons PRIVATE PkgConfig::LIBIDN)
endif()

x2_unit_test(replace)

# The correction module inside the real pipeline, with stubbed delivery.
x2_unit_test(correct
    ${PROJECT_SOURCE_DIR}/src/ext/correct.c
    ${PROJECT_SOURCE_DIR}/src/ext/ext.c
    ${PROJECT_SOURCE_DIR}/src/ext/msgctx.c
    ${PROJECT_SOURCE_DIR}/src/jutil.c)
target_include_directories(test-correct PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(test-correct PRIVATE
    PURPLE_PLUGINS X2_EXT_TEST_TABLE=x2_test_exts)
target_compile_options(test-correct PRIVATE -Wno-unused-parameter)
target_link_libraries(test-correct PRIVATE PkgConfig::LIBXML)
if(XMPP2_IDN)
    target_compile_definitions(test-correct PRIVATE USE_IDN)
    target_link_libraries(test-correct PRIVATE PkgConfig::LIBIDN)
endif()
