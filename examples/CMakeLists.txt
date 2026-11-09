# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
#
# mc_cli, a command-line client to try the core by hand through its C API
# (docs/code-tour.md). The tests below run it on the example commands, so it is also
# built and checked with sanitizers by tools/ci/ci.sh.

add_executable(mc_cli mc_cli.c)
target_link_libraries(mc_cli PRIVATE mc::core)
set_target_properties(mc_cli PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
# MSVC deprecates fopen in favor of fopen_s, which is not portable C.
target_compile_definitions(mc_cli PRIVATE $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
mc_apply_compiler_options(mc_cli)

set(_mc_test_pack "${PROJECT_SOURCE_DIR}/content/test/rules-pack.test.json")
set(_mc_commands "${CMAKE_CURRENT_SOURCE_DIR}/commands")
# Each test passes if the reply contains the expected text (the exit status is not checked),
# and fails on a sanitizer report even if the expected reply was printed before it.
set(_mc_sanitizer_report "Sanitizer|runtime error")

foreach(_mc_case
        "pack_info|pack-info.json|\"id\":\"mc-test\""
        "valid_deck|valid-deck.json|\"valid\":true"
        "invalid_deck|invalid-deck.json|\"rule\":\"legacy_mismatch\""
        "unknown_command|unknown-command.json|\"error\":\"unknown_command\"")
    string(REPLACE "|" ";" _mc_case "${_mc_case}")
    list(GET _mc_case 0 _mc_name)
    list(GET _mc_case 1 _mc_file)
    list(GET _mc_case 2 _mc_expected)
    add_test(NAME mc_cli.${_mc_name} COMMAND mc_cli "${_mc_test_pack}" "${_mc_commands}/${_mc_file}")
    set_tests_properties(mc_cli.${_mc_name} PROPERTIES
        PASS_REGULAR_EXPRESSION "${_mc_expected}"
        FAIL_REGULAR_EXPRESSION "${_mc_sanitizer_report}")
endforeach()
