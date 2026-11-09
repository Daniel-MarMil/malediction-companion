# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
#
# Core tests (RNF-17): GoogleTest for concrete cases and RapidCheck for properties of
# deck validation.

find_package(GTest CONFIG REQUIRED)
find_package(rapidcheck CONFIG REQUIRED)
include(GoogleTest)

# The API header is also compiled as C11: if it stops being valid C, the build fails here.
add_library(mc_c_header_check OBJECT c_header_check.c)
set_target_properties(mc_c_header_check PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
target_link_libraries(mc_c_header_check PRIVATE mc::core)
mc_apply_compiler_options(mc_c_header_check)

add_executable(mc_core_tests
    card_test.cpp
    rules_pack_test.cpp
    deck_validator_test.cpp
    deck_properties_test.cpp
    signature_test.cpp
    trusted_keys_test.cpp
    c_api_test.cpp
    $<TARGET_OBJECTS:mc_c_header_check>)
target_include_directories(mc_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mc_core_tests PRIVATE
    mc::core
    ${MC_SODIUM_TARGET}
    nlohmann_json::nlohmann_json
    rapidcheck
    GTest::gtest
    GTest::gtest_main)
target_compile_definitions(mc_core_tests PRIVATE
    MC_TEST_PACK_PATH="${PROJECT_SOURCE_DIR}/content/test/rules-pack.test.json"
    MC_EXPECTED_VERSION="${PROJECT_VERSION}")
mc_apply_compiler_options(mc_core_tests)

# PRE_TEST lists the tests when ctest runs, so the build does not need to run the
# binary (useful with sanitizers and when cross-compiling).
gtest_discover_tests(mc_core_tests DISCOVERY_MODE PRE_TEST)

# The development key reaches the trusted list only with mc_trust_dev_key ON. Two small
# executables compile their own copy of trusted_keys.cpp with a test key from a fixed seed
# (tests/data/test-dev-key.pub), once with ON and once with OFF (ADR-06).
set(_mc_test_dev_key "${CMAKE_CURRENT_SOURCE_DIR}/data/test-dev-key.pub")
foreach(_mc_variant dev release)
    set(_mc_target mc_trusted_keys_${_mc_variant}_test)
    add_executable(${_mc_target}
        trusted_keys_build_test.cpp
        ${PROJECT_SOURCE_DIR}/core/src/trusted_keys.cpp
        ${PROJECT_SOURCE_DIR}/core/src/signature.cpp)
    target_include_directories(${_mc_target} PRIVATE ${PROJECT_SOURCE_DIR}/core/include)
    target_link_libraries(${_mc_target} PRIVATE ${MC_SODIUM_TARGET} GTest::gtest GTest::gtest_main)
    if(_mc_variant STREQUAL "dev")
        mc_trust_dev_key(${_mc_target} ON "${_mc_test_dev_key}")
        target_compile_definitions(${_mc_target} PRIVATE MC_TEST_EXPECT_DEV_KEY=1)
    else()
        mc_trust_dev_key(${_mc_target} OFF "${_mc_test_dev_key}")
        target_compile_definitions(${_mc_target} PRIVATE MC_TEST_EXPECT_DEV_KEY=0)
    endif()
    mc_apply_compiler_options(${_mc_target})
    gtest_discover_tests(${_mc_target} DISCOVERY_MODE PRE_TEST TEST_PREFIX "${_mc_variant}.")
endforeach()

# Which build types may trust the development key: only Debug, where it is on by default.
set(_mc_trust_project "${CMAKE_CURRENT_SOURCE_DIR}/cmake/trusted_keys")
foreach(_mc_case
        "refused_in_release|Release|-DMC_TRUST_DEV_KEY=ON|only allowed in single-configuration Debug builds"
        "off_by_default_in_release|Release||Development key: not trusted \\(MC_TRUST_DEV_KEY is OFF\\)"
        "on_by_default_in_debug|Debug||Development key: trusted"
        "can_be_turned_off_in_debug|Debug|-DMC_TRUST_DEV_KEY=OFF|Development key: not trusted \\(MC_TRUST_DEV_KEY is OFF\\)")
    string(REPLACE "|" ";" _mc_case "${_mc_case}")
    list(GET _mc_case 0 _mc_name)
    list(GET _mc_case 1 _mc_build_type)
    list(GET _mc_case 2 _mc_option)
    list(GET _mc_case 3 _mc_expected)
    add_test(NAME cmake.dev_key_${_mc_name}
        COMMAND ${CMAKE_COMMAND} --fresh -S "${_mc_trust_project}"
            -B "${CMAKE_CURRENT_BINARY_DIR}/trusted_keys_${_mc_name}"
            -DCMAKE_BUILD_TYPE=${_mc_build_type} ${_mc_option}
            "-DMC_DEV_PUBLIC_KEY_FILE=${_mc_test_dev_key}")
    set_tests_properties(cmake.dev_key_${_mc_name} PROPERTIES PASS_REGULAR_EXPRESSION "${_mc_expected}")
endforeach()

# mc_pack_tool, when it is built (not on Windows): keygen, sign and verify with low Argon2id limits.
if(TARGET mc_pack_tool_lib)
    add_executable(mc_pack_tool_tests pack_tool_test.cpp)
    target_link_libraries(mc_pack_tool_tests PRIVATE
        mc_pack_tool_lib
        nlohmann_json::nlohmann_json
        GTest::gtest
        GTest::gtest_main)
    mc_apply_compiler_options(mc_pack_tool_tests)
    gtest_discover_tests(mc_pack_tool_tests DISCOVERY_MODE PRE_TEST)
endif()
