# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

project(seekers_companion
    VERSION 0.1.0
    DESCRIPTION "Compañero digital para partidas de Malediction: núcleo de reglas en C++"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MC_BUILD_TESTS "Compilar las pruebas del núcleo" ON)
option(MC_BUILD_FUZZERS "Compilar los objetivos de fuzzing con libFuzzer (solo Clang)" OFF)
option(MC_WARNINGS_AS_ERRORS "Avisos del compilador como errores (RNF-19)" ON)
option(MC_COVERAGE "Instrumentar el código para medir la cobertura con gcov" OFF)
set(MC_SANITIZERS "" CACHE STRING "Sanitizers separados por comas, por ejemplo: address,undefined")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(McCompilerOptions)

if(MC_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "MC_BUILD_FUZZERS necesita Clang, que trae libFuzzer")
    endif()
    # El núcleo se instrumenta para que libFuzzer sepa qué caminos ha recorrido.
    add_compile_options(-fsanitize=fuzzer-no-link)
endif()

# Las dependencias se buscan aquí para que sus objetivos se vean en todos los directorios.
find_package(nlohmann_json 3.11 CONFIG REQUIRED)
include(McFindSodium)

add_subdirectory(core)

# Las pruebas se ejecutan en el equipo de desarrollo y en la integración continua,
# no en las compilaciones para Android.
if(MC_BUILD_TESTS AND NOT ANDROID AND NOT BUILD_SHARED_LIBS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(MC_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()
