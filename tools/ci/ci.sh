#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
#
# Tareas del núcleo para la integración continua y para el entorno local (WSL o Linux).
# En la CI corren como root dentro de ubuntu:24.04 e instalan lo que falta; en local usan
# lo que dejó tools/dev/setup-wsl.sh.
#
# Uso: tools/ci/ci.sh TAREA [TAREA...]
#   asan | tsan | release   compila con ese preset y pasa las pruebas
#   coverage                pruebas con cobertura; falla por debajo del 90 % (RNF-17)
#   fuzz                    compila los fuzzers y ejecuta cada uno MC_FUZZ_SECONDS segundos (RNF-19)
#   lint                    clang-format y clang-tidy
#   android                 biblioteca para arm64-v8a y x86_64 y comprobación de 16 KB (RNF-20)
#   licenses                cabeceras SPDX y licencias de las dependencias de vcpkg (RNF-15)
#   all                     todas las anteriores, en ese orden
#   ndk                     solo descarga el NDK r30 si falta (lo usa tools/dev/setup-wsl.sh)
#
# Variables opcionales:
#   MC_BUILD_ROOT      carpeta de compilación (por defecto, build/ en el repositorio)
#   MC_FUZZ_SECONDS    duración de cada fuzzer (por defecto, 120)
#   MC_CMAKE_ARGS      opciones extra para CMake al configurar, separadas por espacios
#   VCPKG_ROOT         vcpkg ya instalado (por defecto, .vcpkg/ en el repositorio)
#   ANDROID_NDK_HOME   NDK ya instalado (si no, se descarga el r30 en MC_NDK_DIR o .ndk/)
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"

export DEBIAN_FRONTEND=noninteractive
export VCPKG_DISABLE_METRICS=1
export VCPKG_ROOT="${VCPKG_ROOT:-$root/.vcpkg}"
export VCPKG_DEFAULT_BINARY_CACHE="${VCPKG_DEFAULT_BINARY_CACHE:-$root/.vcpkg-cache}"
# Las mismas opciones que los presets de pruebas de CMakePresets.json.
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:abort_on_error=1:strict_string_checks=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}"
export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1}"

build_root="${MC_BUILD_ROOT:-$root/build}"
fuzz_seconds="${MC_FUZZ_SECONDS:-120}"
read -r -a cmake_args <<< "${MC_CMAKE_ARGS:-}"

# NDK r30 (LTS), según la página de descargas del NDK consultada el 07-11-2026.
ndk_version="r30"
ndk_sha1="5107f898313790e449e87eee2183d9a20602dee9"

usage() {
    sed -n '9,24p' "${BASH_SOURCE[0]}"
}

install_packages() {
    if [ "$(id -u)" -ne 0 ]; then
        return  # En local, las herramientas las instala tools/dev/setup-wsl.sh.
    fi
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends \
        build-essential clang libclang-rt-18-dev clang-tidy clang-format cmake ninja-build git curl \
        ca-certificates zip unzip tar pkg-config python3 python3-pip autoconf autoconf-archive automake libtool
}

setup_done=false
setup() {
    if [ "$setup_done" = true ]; then
        return
    fi
    install_packages
    mkdir -p "$VCPKG_DEFAULT_BINARY_CACHE"
    tools/ci/bootstrap-vcpkg.sh "$VCPKG_ROOT"
    setup_done=true
}

# Configura, compila y, salvo con "sin-pruebas", ejecuta las pruebas de un preset.
# La carpeta de compilación se fija con -B para poder sacarla del repositorio (MC_BUILD_ROOT).
build_preset() {
    local preset="$1" dir="$build_root/$1"
    cmake --preset "$preset" -B "$dir" "${cmake_args[@]}"
    cmake --build "$dir"
    if [ "${2:-}" != "sin-pruebas" ]; then
        ctest --test-dir "$dir" --output-on-failure --no-tests=error
    fi
}

ensure_gcovr() {
    if command -v gcovr > /dev/null; then
        return
    fi
    if [ "$(id -u)" -eq 0 ]; then
        pip install --quiet --break-system-packages gcovr==8.8
    else
        python3 -m pip install --quiet --user --break-system-packages gcovr==8.8
        export PATH="$HOME/.local/bin:$PATH"
    fi
}

ensure_ndk() {
    if [ -n "${ANDROID_NDK_HOME:-}" ] && [ -d "$ANDROID_NDK_HOME" ]; then
        return
    fi
    local base="${MC_NDK_DIR:-$root/.ndk}"
    export ANDROID_NDK_HOME="$base/android-ndk-$ndk_version"
    if [ ! -d "$ANDROID_NDK_HOME" ]; then
        mkdir -p "$base"
        curl -fsSLo "$base/ndk.zip" "https://dl.google.com/android/repository/android-ndk-$ndk_version-linux.zip"
        echo "$ndk_sha1  $base/ndk.zip" | sha1sum -c -
        unzip -q "$base/ndk.zip" -d "$base"
        rm "$base/ndk.zip"
    fi
}

run_fuzzer() {
    local name="$1" seeds="$2" corpus
    corpus="$(mktemp -d)"
    cp "$seeds"/*.json "$corpus/"
    "$build_root/fuzz/fuzz/mc_fuzz_$name" -max_total_time="$fuzz_seconds" "$corpus"
}

run_task() {
    case "$1" in
        asan | tsan | release)
            setup
            build_preset "$1"
            ;;
        coverage)
            setup
            ensure_gcovr
            build_preset coverage
            mkdir -p "$build_root/coverage/html"
            gcovr --root . --filter core/src/ --object-directory "$build_root/coverage" --txt --print-summary \
                --html-details "$build_root/coverage/html/index.html" --fail-under-line 90
            echo "Informe de cobertura: $build_root/coverage/html/index.html"
            ;;
        fuzz)
            setup
            build_preset fuzz sin-pruebas
            run_fuzzer dispatch fuzz/corpus/dispatch
            run_fuzzer rules_pack content/test
            ;;
        lint)
            setup
            mapfile -t sources < <(find core tests fuzz -name '*.[ch]' -o -name '*.[ch]pp' | sort)
            clang-format --dry-run --Werror "${sources[@]}"
            cmake --preset tidy -B "$build_root/tidy" "${cmake_args[@]}"
            clang-tidy -p "$build_root/tidy" --quiet --warnings-as-errors='*' core/src/*.cpp
            ;;
        android)
            setup
            ensure_ndk
            build_preset android-arm64 sin-pruebas
            build_preset android-x86_64 sin-pruebas
            python3 tools/ci/check_page_alignment.py "$build_root/android-arm64/core" "$build_root/android-x86_64/core"
            ;;
        licenses)
            python3 tools/ci/check_licenses.py
            # Reutiliza las dependencias de las compilaciones que ya hay; si no hay ninguna, compila release.
            local installed=()
            for dir in "$build_root"/*/vcpkg_installed; do
                if [ -d "$dir" ]; then
                    installed+=("$dir")
                fi
            done
            if [ "${#installed[@]}" -eq 0 ]; then
                setup
                cmake --preset release -B "$build_root/release" "${cmake_args[@]}"
                installed=("$build_root/release/vcpkg_installed")
            fi
            for dir in "${installed[@]}"; do
                python3 tools/ci/check_licenses.py --vcpkg-installed "$dir"
            done
            ;;
        ndk)
            ensure_ndk
            echo "NDK en $ANDROID_NDK_HOME"
            ;;
        all)
            local task
            for task in lint asan tsan release coverage fuzz android licenses; do
                run_task "$task"
            done
            ;;
        *)
            usage
            exit 2
            ;;
    esac
}

if [ "$#" -eq 0 ]; then
    usage
    exit 2
fi
for task in "$@"; do
    echo "== $task"
    run_task "$task"
done
echo "== Tareas completadas: $*"
