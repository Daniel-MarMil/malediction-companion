#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
#
# Prepara Ubuntu 24.04 (en WSL 2 o nativo) para compilar y probar el núcleo igual que la
# integración continua. Se ejecuta una vez, como usuario normal (pide sudo para apt):
#   tools/dev/setup-wsl.sh
#
# Deja vcpkg y el NDK en ~/.local/share, las compilaciones en ~/.cache/malediction-companion
# y las variables en ~/.bashrc. Se puede repetir sin problemas para actualizar.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
data_dir="${XDG_DATA_HOME:-$HOME/.local/share}"
cache_dir="${XDG_CACHE_HOME:-$HOME/.cache}"
bin_dir="$HOME/.local/bin"

vcpkg_root="$data_dir/vcpkg"
ndk_dir="$data_dir/android-ndk"
build_root="$cache_dir/malediction-companion/build"

# pipx y Woodpecker CLI van a ~/.local/bin. Con esa carpeta ya en el PATH, pipx no avisa de
# que falta y las herramientas sirven durante el resto del script.
mkdir -p "$bin_dir"
export PATH="$bin_dir:$PATH"

# Woodpecker CLI para validar los flujos de .woodpecker/ (versión y suma de la publicación oficial).
woodpecker_version="3.20.1"
woodpecker_sha256="5b74ff1808af35b57bd5c2b528b7fdff24f5f69ad327715c1797f764da79dc13"

step() {
    printf '\n== %s\n' "$1"
}

if [ "$(id -u)" -eq 0 ]; then
    echo "Ejecuta este script con tu usuario, no como root: pedirá sudo cuando haga falta." >&2
    exit 1
fi
if ! grep -q 'VERSION_ID="24.04"' /etc/os-release 2> /dev/null; then
    echo "Aviso: la integración continua usa Ubuntu 24.04 y este sistema es otro; puede haber diferencias." >&2
fi

step "Paquetes del sistema"
sudo apt-get update -qq
sudo apt-get install -y -qq --no-install-recommends \
    build-essential clang libclang-rt-18-dev clang-tidy clang-format cmake ninja-build git curl \
    ca-certificates zip unzip tar pkg-config python3 python3-pip pipx \
    autoconf autoconf-archive automake libtool gdb valgrind cppcheck shellcheck yamllint

step "gcovr 8.8"
pipx install --force gcovr==8.8 > /dev/null
pipx ensurepath > /dev/null

step "vcpkg en $vcpkg_root"
"$root/tools/ci/bootstrap-vcpkg.sh" "$vcpkg_root"

step "NDK r30 en $ndk_dir"
ANDROID_NDK_HOME="" MC_NDK_DIR="$ndk_dir" "$root/tools/ci/ci.sh" ndk

step "Woodpecker CLI $woodpecker_version"
mkdir -p "$bin_dir"
tmp="$(mktemp -d)"
curl -fsSLo "$tmp/woodpecker-cli.tar.gz" \
    "https://github.com/woodpecker-ci/woodpecker/releases/download/v$woodpecker_version/woodpecker-cli_linux_amd64.tar.gz"
echo "$woodpecker_sha256  $tmp/woodpecker-cli.tar.gz" | sha256sum -c -
tar -xzf "$tmp/woodpecker-cli.tar.gz" -C "$tmp"
install -m 0755 "$tmp/woodpecker-cli" "$bin_dir/woodpecker-cli"
rm -rf "$tmp"

step "Variables en ~/.bashrc"
mkdir -p "$build_root"
begin="# >>> malediction-companion >>>"
end="# <<< malediction-companion <<<"
touch "$HOME/.bashrc"
sed -i "/^$begin\$/,/^$end\$/d" "$HOME/.bashrc"
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

step "Listo"
cmake --version | head -1
clang --version | head -1
g++ --version | head -1
"$bin_dir/woodpecker-cli" --version 2> /dev/null || true
cat << EOF

Abre una terminal nueva (o ejecuta: source ~/.bashrc) y prueba:
  cd "$root"
  tools/ci/ci.sh asan
La primera vez vcpkg compila las dependencias y tarda unos minutos; después quedan en caché.
EOF
