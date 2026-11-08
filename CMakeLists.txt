# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

project(seekers_companion
    VERSION 0.1.0
    DESCRIPTION "Digital companion for Malediction games: rules core in C++"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MC_BUILD_TESTS "Build the core tests" ON)
option(MC_BUILD_FUZZERS "Build the libFuzzer fuzzing targets (Clang only)" OFF)
option(MC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (RNF-19)" ON)
option(MC_COVERAGE "Instrument the code to measure coverage with gcov" OFF)
set(MC_SANITIZERS "" CACHE STRING "Comma-separated sanitizers, for example: address,undefined")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(McCompilerOptions)

if(MC_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "MC_BUILD_FUZZERS needs Clang, which ships libFuzzer")
    endif()
    # The core is instrumented so that libFuzzer knows which paths it has explored.
    add_compile_options(-fsanitize=fuzzer-no-link)
endif()

# Dependencies are found here so that their targets are visible in every directory.
find_package(nlohmann_json 3.11 CONFIG REQUIRED)
include(McFindSodium)

add_subdirectory(core)

# Tests run on the development machine and in continuous integration,
# not in Android builds. mc_cli, the command-line client in examples/, is built and
# tested with them.
if(MC_BUILD_TESTS AND NOT ANDROID AND NOT BUILD_SHARED_LIBS)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(examples)
endif()

if(MC_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()
