#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
#
# Descarga vcpkg en el commit de la línea base de vcpkg.json y lo prepara sin telemetría.
# Uso: tools/ci/bootstrap-vcpkg.sh [carpeta]   (por defecto, .vcpkg en la raíz del repositorio)
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
target="${1:-$root/.vcpkg}"
baseline="$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["builtin-baseline"])' "$root/vcpkg.json")"

if [ ! -d "$target/.git" ]; then
    git clone --quiet https://github.com/microsoft/vcpkg.git "$target"
fi
if ! git -C "$target" cat-file -e "$baseline^{commit}" 2>/dev/null; then
    git -C "$target" fetch --quiet origin
fi
git -C "$target" -c advice.detachedHead=false checkout --quiet "$baseline"
"$target/bootstrap-vcpkg.sh" -disableMetrics

echo "vcpkg listo en $target (línea base $baseline)"
