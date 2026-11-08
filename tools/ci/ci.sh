#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
#
# Core tasks for continuous integration and for the local environment (WSL or Linux).
# In CI they run as root inside ubuntu:26.04 and install whatever is missing; locally they
# use what tools/dev/setup-wsl.sh installed.
#
# Usage: tools/ci/ci.sh TASK [TASK...]
#   asan | tsan | release   build with that preset and run the tests
#   coverage                tests with coverage; fails below 90 % (RNF-17)
#   fuzz                    build the fuzzers and run each one for MC_FUZZ_SECONDS seconds (RNF-19)
#   lint                    clang-format and clang-tidy
#   android                 library for arm64-v8a and x86_64, plus the 16 KB check (RNF-20)
#   licenses                SPDX headers and licenses of the vcpkg dependencies (RNF-15)
#   all                     all of the above, in that order
#   ndk                     only downloads NDK r30 if it is missing (used by tools/dev/setup-wsl.sh)
#
# Optional variables:
#   MC_BUILD_ROOT      build folder (default: build/ in the repository)
#   MC_FUZZ_SECONDS    how long each fuzzer runs (default: 120)
#   MC_CMAKE_ARGS      extra CMake options at configure time, separated by spaces
#   VCPKG_ROOT         an existing vcpkg (default: .vcpkg/ in the repository)
#   ANDROID_NDK_HOME   an existing NDK (otherwise r30 is downloaded to MC_NDK_DIR or .ndk/)
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"

export DEBIAN_FRONTEND=noninteractive
export VCPKG_DISABLE_METRICS=1
export VCPKG_ROOT="${VCPKG_ROOT:-$root/.vcpkg}"
export VCPKG_DEFAULT_BINARY_CACHE="${VCPKG_DEFAULT_BINARY_CACHE:-$root/.vcpkg-cache}"
# The same options as the test presets in CMakePresets.json.
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:abort_on_error=1:strict_string_checks=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}"
export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1}"

build_root="${MC_BUILD_ROOT:-$root/build}"
fuzz_seconds="${MC_FUZZ_SECONDS:-120}"
read -r -a cmake_args <<< "${MC_CMAKE_ARGS:-}"

# NDK r30 (LTS), according to the NDK downloads page checked on 2026-11-07.
ndk_version="r30"
ndk_sha1="5107f898313790e449e87eee2183d9a20602dee9"

usage() {
    sed -n '9,24p' "${BASH_SOURCE[0]}"
}

install_packages() {
    if [ "$(id -u)" -ne 0 ]; then
        return  # Locally, tools/dev/setup-wsl.sh installs the tools.
    fi
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends \
        build-essential clang libclang-rt-dev clang-tidy clang-format cmake ninja-build git curl \
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

# Configures and builds a preset and, unless "no-tests" is given, runs its tests.
# The build folder is set with -B so it can live outside the repository (MC_BUILD_ROOT).
build_preset() {
    local preset="$1" dir="$build_root/$1"
    cmake --preset "$preset" -B "$dir" "${cmake_args[@]}"
    cmake --build "$dir"
    if [ "${2:-}" != "no-tests" ]; then
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
            # Only this build's data: no search through the repository, which is slow on WSL and
            # may hold stale data from other builds.
            gcovr --root . --filter core/src/ --object-directory "$build_root/coverage" --txt --print-summary \
                --html-details "$build_root/coverage/html/index.html" --fail-under-line 90 "$build_root/coverage"
            echo "Coverage report: $build_root/coverage/html/index.html"
            ;;
        fuzz)
            setup
            build_preset fuzz no-tests
            run_fuzzer dispatch fuzz/corpus/dispatch
            run_fuzzer rules_pack content/test
            ;;
        lint)
            setup
            mapfile -t sources < <(find core tests fuzz examples -name '*.[ch]' -o -name '*.[ch]pp' | sort)
            clang-format --dry-run --Werror "${sources[@]}"
            cmake --preset tidy -B "$build_root/tidy" "${cmake_args[@]}"
            clang-tidy -p "$build_root/tidy" --quiet --warnings-as-errors='*' core/src/*.cpp
            ;;
        android)
            setup
            ensure_ndk
            build_preset android-arm64 no-tests
            build_preset android-x86_64 no-tests
            python3 tools/ci/check_page_alignment.py "$build_root/android-arm64/core" "$build_root/android-x86_64/core"
            ;;
        licenses)
            python3 tools/ci/check_licenses.py
            # Reuses the dependencies of existing builds; if there are none, it configures release.
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
            echo "NDK at $ANDROID_NDK_HOME"
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
echo "== Tasks completed: $*"
