# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
#
# Trusted public keys of the core (ADR-06). The release keys are in core/src/trusted_keys.cpp.
# The development key is added only to Debug builds with MC_TRUST_DEV_KEY, which is on by
# default in Debug and refused in any other build type, so release builds never trust it.

# Adds the development key to a target that compiles core/src/trusted_keys.cpp.
#   mc_trust_dev_key(<target> <ON|OFF> <public key file>)
# With OFF, or when the file does not exist yet, the target trusts only the release keys.
function(mc_trust_dev_key target enabled key_file)
    if(NOT enabled)
        return()
    endif()
    if(NOT EXISTS "${key_file}")
        return()
    endif()
    file(READ "${key_file}" _mc_key)
    string(STRIP "${_mc_key}" _mc_key)
    if(NOT _mc_key MATCHES "^[0-9a-fA-F]+$")
        message(FATAL_ERROR "${key_file} must hold one Ed25519 public key in hexadecimal")
    endif()
    string(LENGTH "${_mc_key}" _mc_key_length)
    if(NOT _mc_key_length EQUAL 64)
        message(FATAL_ERROR "${key_file} must hold one Ed25519 public key in hexadecimal")
    endif()
    target_compile_definitions(${target} PRIVATE MC_DEV_PUBLIC_KEY_HEX="${_mc_key}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${key_file}")
endfunction()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_mc_trust_dev_key_default ON)
else()
    set(_mc_trust_dev_key_default OFF)
endif()
option(MC_TRUST_DEV_KEY "Trust the development key for packs (Debug builds only, ADR-06)" ${_mc_trust_dev_key_default})
set(MC_DEV_PUBLIC_KEY_FILE "${PROJECT_SOURCE_DIR}/keys/dev.pub" CACHE FILEPATH
    "Public key of the development key, in hexadecimal (written by mc_pack_tool keygen)")

if(MC_TRUST_DEV_KEY)
    get_property(_mc_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(_mc_multi_config OR NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(FATAL_ERROR "MC_TRUST_DEV_KEY is only allowed in single-configuration Debug builds (ADR-06)")
    endif()
    if(EXISTS "${MC_DEV_PUBLIC_KEY_FILE}")
        message(STATUS "Development key: trusted (${MC_DEV_PUBLIC_KEY_FILE})")
    else()
        message(STATUS "Development key: not trusted, ${MC_DEV_PUBLIC_KEY_FILE} does not exist yet")
    endif()
else()
    message(STATUS "Development key: not trusted (MC_TRUST_DEV_KEY is OFF)")
endif()
