# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
#
# Fuzzing con libFuzzer de todo lo que llega al núcleo desde fuera (RNF-19): paquetes de
# reglas y comandos JSON. Ejemplo, durante un minuto:
#   build/fuzz/fuzz/mc_fuzz_dispatch -max_total_time=60 fuzz/corpus/dispatch

foreach(name rules_pack dispatch)
    add_executable(mc_fuzz_${name} fuzz_${name}.cpp)
    target_link_libraries(mc_fuzz_${name} PRIVATE mc::core)
    target_link_options(mc_fuzz_${name} PRIVATE -fsanitize=fuzzer)
    target_compile_definitions(mc_fuzz_${name} PRIVATE
        MC_TEST_PACK_PATH="${PROJECT_SOURCE_DIR}/content/test/rules-pack.test.json")
    mc_apply_compiler_options(mc_fuzz_${name})
endforeach()
