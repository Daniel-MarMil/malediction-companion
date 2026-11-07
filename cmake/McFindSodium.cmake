# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
#
# Busca libsodium (ADR-06) y deja el nombre del objetivo en MC_SODIUM_TARGET.
# Primero prueba el paquete de vcpkg; si no está, usa pkg-config para compilar con la
# biblioteca del sistema, como hacen las distribuciones de Linux.

find_package(unofficial-sodium CONFIG QUIET)
if(TARGET unofficial-sodium::sodium)
    set(MC_SODIUM_TARGET unofficial-sodium::sodium)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MC_SODIUM REQUIRED IMPORTED_TARGET GLOBAL libsodium>=1.0.18)
    set(MC_SODIUM_TARGET PkgConfig::MC_SODIUM)
endif()
message(STATUS "libsodium: ${MC_SODIUM_TARGET}")
