# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
#
# mc_pack_tool (ADR-06): keygen, sign and verify for rules packs. A development tool: it is
# not part of mc_core, is not exported and is not built for Android or Windows.
# mc_pack_tool_lib holds the commands so that the tests can call them with low Argon2id limits.

add_library(mc_pack_tool_lib STATIC
    hex.cpp
    key_file.cpp
    pack_tool.cpp
    secure_buffer.cpp)
target_include_directories(mc_pack_tool_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mc_pack_tool_lib PUBLIC cxx_std_20)
target_link_libraries(mc_pack_tool_lib
    PUBLIC mc::core ${MC_SODIUM_TARGET}
    PRIVATE nlohmann_json::nlohmann_json)
mc_apply_compiler_options(mc_pack_tool_lib)

add_executable(mc_pack_tool main.cpp terminal_password.cpp)
target_link_libraries(mc_pack_tool PRIVATE mc_pack_tool_lib)
mc_apply_compiler_options(mc_pack_tool)
