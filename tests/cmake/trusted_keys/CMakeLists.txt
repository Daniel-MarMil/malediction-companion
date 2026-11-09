# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
#
# A project with no languages that only loads cmake/McTrustedKeys.cmake, so the tests can
# check which build types may trust the development key (tests/CMakeLists.txt).

cmake_minimum_required(VERSION 3.25)
project(mc_trusted_keys_check NONE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake")
include(McTrustedKeys)
