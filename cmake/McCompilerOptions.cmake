# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
#
# Common build options. RNF-19: warnings as errors, sanitizers and coverage.
# RNF-20: Android libraries aligned to 16 KB pages.

if(MC_SANITIZERS)
    if(MSVC)
        message(WARNING "MC_SANITIZERS only works with GCC and Clang; it is ignored with MSVC")
    else()
        string(REPLACE "," ";" _mc_sanitizer_list "${MC_SANITIZERS}")
        list(JOIN _mc_sanitizer_list "," _mc_sanitizer_flag)
        add_compile_options(-fsanitize=${_mc_sanitizer_flag} -fno-omit-frame-pointer -fno-sanitize-recover=all)
        add_link_options(-fsanitize=${_mc_sanitizer_flag})
    endif()
endif()

if(MC_COVERAGE)
    if(MSVC)
        message(WARNING "MC_COVERAGE only works with GCC and Clang; it is ignored with MSVC")
    else()
        add_compile_options(--coverage -O0 -g)
        add_link_options(--coverage)
    endif()
endif()

if(ANDROID)
    # NDK r28 and later already align to 16 KB; older versions need these options.
    add_link_options(-Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384)
endif()

# Strict warnings for our own code. vcpkg dependencies come in as system headers,
# so their warnings do not count.
function(mc_apply_compiler_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8 $<$<COMPILE_LANGUAGE:CXX>:/permissive->)
        if(MC_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
            -Wcast-align -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough
            $<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual>)
        # With GCC, -Wnull-dereference gives false positives at -O2 and -O3 inside inlined
        # third-party code (for example, nlohmann::json), so it is only used with Clang.
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${target} PRIVATE -Wnull-dereference)
        endif()
        if(MC_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
