#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
#
# Prepares Ubuntu 26.04 (on WSL 2 or native) to build and test the core with tools/ci/ci.sh.
# Run it once, as a normal user (it asks for sudo for apt):
#   tools/dev/setup-wsl.sh
#
# It leaves vcpkg and the NDK in ~/.local/share, the builds in ~/.cache/seekers-companion
# and the variables in ~/.bashrc. It is safe to run again to update.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
data_dir="${XDG_DATA_HOME:-$HOME/.local/share}"
cache_dir="${XDG_CACHE_HOME:-$HOME/.cache}"
bin_dir="$HOME/.local/bin"

vcpkg_root="$data_dir/vcpkg"
ndk_dir="$data_dir/android-ndk"
build_root="$cache_dir/seekers-companion/build"

# pipx installs gcovr in ~/.local/bin. With that folder already on the PATH, pipx does
# not warn that it is missing and the tools work for the rest of the script.
mkdir -p "$bin_dir"
export PATH="$bin_dir:$PATH"

step() {
    printf '\n== %s\n' "$1"
}

if [ "$(id -u)" -eq 0 ]; then
    echo "Run this script as your own user, not as root: it asks for sudo when needed." >&2
    exit 1
fi
if ! grep -q 'VERSION_ID="26.04"' /etc/os-release 2> /dev/null; then
    echo "Warning: the reference environment is Ubuntu 26.04 and this system is different; results may differ." >&2
fi

step "System packages"
sudo apt-get update -qq
sudo apt-get install -y -qq --no-install-recommends \
    build-essential clang libclang-rt-dev clang-tidy clang-format clangd cmake ninja-build git curl \
    ca-certificates zip unzip tar pkg-config python3 python3-pip pipx \
    autoconf autoconf-archive automake libtool gdb valgrind cppcheck shellcheck yamllint

step "gcovr 8.8"
pipx install --force gcovr==8.8 > /dev/null
pipx ensurepath > /dev/null

step "vcpkg at $vcpkg_root"
"$root/tools/ci/bootstrap-vcpkg.sh" "$vcpkg_root"

step "NDK r30 at $ndk_dir"
ANDROID_NDK_HOME="" MC_NDK_DIR="$ndk_dir" "$root/tools/ci/ci.sh" ndk

step "Variables in ~/.bashrc"
mkdir -p "$build_root"
begin="# >>> seekers-companion >>>"
end="# <<< seekers-companion <<<"
touch "$HOME/.bashrc"
# Remove the previous block, including the ones left by the project's old names.
for name in seekers-companion seeker-companion malediction-companion; do
    sed -i "/^# >>> $name >>>\$/,/^# <<< $name <<<\$/d" "$HOME/.bashrc"
done
cat >> "$HOME/.bashrc" << EOF
$begin
export VCPKG_ROOT="$vcpkg_root"
export VCPKG_DISABLE_METRICS=1
export VCPKG_DEFAULT_BINARY_CACHE="$cache_dir/vcpkg/archives"
export ANDROID_NDK_HOME="$ndk_dir/android-ndk-r30"
export MC_BUILD_ROOT="$build_root"
export PATH="$bin_dir:\$PATH"
$end
EOF
mkdir -p "$cache_dir/vcpkg/archives"

step "Done"
cmake --version | head -1
clang --version | head -1
g++ --version | head -1
cat << EOF

Open a new terminal (or run: source ~/.bashrc) and try:
  cd "$root"
  tools/ci/ci.sh asan
The first time, vcpkg builds the dependencies and takes a few minutes; after that they are cached.
EOF
