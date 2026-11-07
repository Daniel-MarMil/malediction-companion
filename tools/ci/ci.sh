#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
#
# Tareas de la integración continua del núcleo. Las usa .woodpecker/ y también se pueden
# lanzar a mano en Linux o WSL. En la CI se ejecutan como root dentro de ubuntu:24.04.
#
# Uso: tools/ci/ci.sh TAREA
#   asan | tsan | release   compila con ese preset y pasa las pruebas
#   coverage                pruebas con cobertura; falla por debajo del 90 % (RNF-17)
#   fuzz                    compila los fuzzers y ejecuta cada uno dos minutos (RNF-19)
#   lint                    clang-format y clang-tidy
#   android                 biblioteca para arm64-v8a y x86_64 y comprobación de 16 KB (RNF-20)
#   vcpkg-licenses          licencias de las dependencias según el SBOM de vcpkg (RNF-15)
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"

export DEBIAN_FRONTEND=noninteractive
export VCPKG_DISABLE_METRICS=1
export VCPKG_ROOT="$root/.vcpkg"
export VCPKG_DEFAULT_BINARY_CACHE="$root/.vcpkg-cache"

# NDK r30 (LTS), según la página de descargas del NDK consultada el 07-11-2026.
ndk_version="r30"
ndk_sha1="5107f898313790e449e87eee2183d9a20602dee9"

install_packages() {
    if [ "$(id -u)" -ne 0 ]; then
        echo "Sin permisos de root: se usan las herramientas ya instaladas"
        return
    fi
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends \
        build-essential clang libclang-rt-18-dev clang-tidy clang-format cmake ninja-build git curl \
        ca-certificates zip unzip tar pkg-config python3 python3-pip autoconf autoconf-archive automake libtool
}

setup() {
    install_packages
    mkdir -p "$VCPKG_DEFAULT_BINARY_CACHE"
    tools/ci/bootstrap-vcpkg.sh "$VCPKG_ROOT"
}

# Configura, compila y, salvo con "sin-pruebas", ejecuta las pruebas de un preset.
build_preset() {
    cmake --preset "$1"
    cmake --build --preset "$1"
    if [ "${2:-}" != "sin-pruebas" ]; then
        ctest --preset "$1"
    fi
}

run_fuzzer() {
    local name="$1" seeds="$2" corpus
    corpus="$(mktemp -d)"
    cp "$seeds"/*.json "$corpus/"
    "build/fuzz/fuzz/mc_fuzz_$name" -max_total_time=120 "$corpus"
}

task="${1:-}"
case "$task" in
    asan | tsan | release)
        setup
        build_preset "$task"
        ;;
    coverage)
        setup
        pip install --quiet --break-system-packages gcovr==8.8
        build_preset coverage
        gcovr --root . --filter core/src/ --object-directory build/coverage --txt --print-summary \
            --fail-under-line 90
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
        cmake --preset dev -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
        clang-tidy -p build/dev --quiet --warnings-as-errors='*' core/src/*.cpp
        ;;
    android)
        setup
        export ANDROID_NDK_HOME="$root/.ndk/android-ndk-$ndk_version"
        if [ ! -d "$ANDROID_NDK_HOME" ]; then
            mkdir -p "$root/.ndk"
            curl -fsSLo "$root/.ndk/ndk.zip" "https://dl.google.com/android/repository/android-ndk-$ndk_version-linux.zip"
            echo "$ndk_sha1  $root/.ndk/ndk.zip" | sha1sum -c -
            unzip -q "$root/.ndk/ndk.zip" -d "$root/.ndk"
            rm "$root/.ndk/ndk.zip"
        fi
        build_preset android-arm64 sin-pruebas
        build_preset android-x86_64 sin-pruebas
        python3 tools/ci/check_page_alignment.py build/android-arm64/core build/android-x86_64/core
        ;;
    vcpkg-licenses)
        setup
        cmake --preset release
        python3 tools/ci/check_licenses.py --vcpkg-installed build/release/vcpkg_installed
        ;;
    *)
        sed -n '8,14p' "${BASH_SOURCE[0]}"
        exit 2
        ;;
esac
