# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
#
# Opciones de compilación comunes. RNF-19: avisos como errores, sanitizers y cobertura.
# RNF-20: bibliotecas para Android alineadas a páginas de 16 KB.

if(MC_SANITIZERS)
    if(MSVC)
        message(WARNING "MC_SANITIZERS solo funciona con GCC y Clang; se ignora con MSVC")
    else()
        string(REPLACE "," ";" _mc_sanitizer_list "${MC_SANITIZERS}")
        list(JOIN _mc_sanitizer_list "," _mc_sanitizer_flag)
        add_compile_options(-fsanitize=${_mc_sanitizer_flag} -fno-omit-frame-pointer -fno-sanitize-recover=all)
        add_link_options(-fsanitize=${_mc_sanitizer_flag})
    endif()
endif()

if(MC_COVERAGE)
    if(MSVC)
        message(WARNING "MC_COVERAGE solo funciona con GCC y Clang; se ignora con MSVC")
    else()
        add_compile_options(--coverage -O0 -g)
        add_link_options(--coverage)
    endif()
endif()

if(ANDROID)
    # El NDK r28 o posterior ya alinea a 16 KB; con versiones anteriores hacen falta estas opciones.
    add_link_options(-Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384)
endif()

# Avisos estrictos para el código propio. Las dependencias de vcpkg entran como cabeceras
# de sistema, así que sus avisos no cuentan.
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
        # En GCC, -Wnull-dereference da falsos positivos con -O2 y -O3 dentro de código de
        # terceros ya expandido (por ejemplo, nlohmann::json), así que solo se usa con Clang.
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${target} PRIVATE -Wnull-dereference)
        endif()
        if(MC_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
