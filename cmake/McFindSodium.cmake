# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
#
# Finds libsodium (ADR-06) and stores the target name in MC_SODIUM_TARGET.
# It tries the vcpkg package first; if that is missing, it uses pkg-config to build
# against the system library, as Linux distributions do.

find_package(unofficial-sodium CONFIG QUIET)
if(TARGET unofficial-sodium::sodium)
    set(MC_SODIUM_TARGET unofficial-sodium::sodium)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MC_SODIUM REQUIRED IMPORTED_TARGET GLOBAL libsodium>=1.0.18)
    set(MC_SODIUM_TARGET PkgConfig::MC_SODIUM)
endif()
message(STATUS "libsodium: ${MC_SODIUM_TARGET}")
