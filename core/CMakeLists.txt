# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
#
# Shared core: domain and rules, with no user interface and no I/O (ADR-01 and ADR-11).

add_library(mc_core
    src/card.cpp
    src/rules_pack.cpp
    src/deck_validator.cpp
    src/json_io.cpp
    src/signature.cpp
    src/trusted_keys.cpp
    src/mc_api.cpp)
add_library(mc::core ALIAS mc_core)

target_include_directories(mc_core
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(mc_core PUBLIC cxx_std_20)
target_link_libraries(mc_core PRIVATE nlohmann_json::nlohmann_json ${MC_SODIUM_TARGET})
target_compile_definitions(mc_core
    PRIVATE MC_BUILDING_LIBRARY MC_VERSION_STRING="${PROJECT_VERSION}")
if(BUILD_SHARED_LIBS)
    target_compile_definitions(mc_core PUBLIC MC_SHARED)
endif()

# Only the C API is visible from outside the library (ADR-11).
set_target_properties(mc_core PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

# In the shared library (Android, Linux), a symbol list also hides the standard library
# templates that would otherwise be exported.
if(BUILD_SHARED_LIBS AND NOT APPLE AND NOT MSVC)
    set(_mc_symbol_map "${CMAKE_CURRENT_SOURCE_DIR}/mc_core.map")
    target_link_options(mc_core PRIVATE "LINKER:--version-script=${_mc_symbol_map}")
    set_property(TARGET mc_core APPEND PROPERTY LINK_DEPENDS "${_mc_symbol_map}")
endif()

mc_trust_dev_key(mc_core ${MC_TRUST_DEV_KEY} "${MC_DEV_PUBLIC_KEY_FILE}")
mc_apply_compiler_options(mc_core)
