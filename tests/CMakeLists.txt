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
