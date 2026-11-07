# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
#
# Núcleo compartido: dominio y reglas, sin interfaz gráfica ni E/S (ADR-01 y ADR-11).

add_library(mc_core
    src/card.cpp
    src/rules_pack.cpp
    src/deck_validator.cpp
    src/json_io.cpp
    src/signature.cpp
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

# Solo la API en C es visible desde fuera de la biblioteca (ADR-11).
set_target_properties(mc_core PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

# En la biblioteca compartida (Android, Linux), una lista de símbolos oculta también las
# plantillas de la biblioteca estándar que, si no, se exportarían.
if(BUILD_SHARED_LIBS AND NOT APPLE AND NOT MSVC)
    set(_mc_symbol_map "${CMAKE_CURRENT_SOURCE_DIR}/mc_core.map")
    target_link_options(mc_core PRIVATE "LINKER:--version-script=${_mc_symbol_map}")
    set_property(TARGET mc_core APPEND PROPERTY LINK_DEPENDS "${_mc_symbol_map}")
endif()

mc_apply_compiler_options(mc_core)
