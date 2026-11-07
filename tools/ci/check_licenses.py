#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
"""Comprueba las licencias del proyecto (ADR-08 y RNF-15).

1. Cada fichero propio lleva la cabecera SPDX de Apache-2.0, o lo cubre REUSE.toml.
2. Con --vcpkg-installed: las dependencias de vcpkg tienen licencias permitidas, según
   el SBOM SPDX que vcpkg genera para cada paquete.
3. Con --scancode: el informe JSON de ScanCode Toolkit no encuentra GPL, LGPL ni AGPL
   en el código propio.

Uso:
  check_licenses.py [--vcpkg-installed CARPETA] [--scancode INFORME.json]
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LICENSE_ID = "Apache-2.0"

# Licencias permitidas para las dependencias (ADR-08).
ALLOWED = {"Apache-2.0", "MIT", "BSD-2-Clause", "BSD-3-Clause", "ISC", "Zlib", "BSL-1.0", "CC0-1.0", "Unlicense", "0BSD"}
FORBIDDEN = re.compile(r"\b(A|L)?GPL", re.IGNORECASE)

HEADER_SUFFIXES = {".c", ".h", ".cpp", ".hpp", ".cmake", ".py", ".sh", ".yaml", ".yml", ".toml", ".md", ".map"}
HEADER_NAMES = {"CMakeLists.txt", ".clang-format", ".clang-tidy", ".gitignore", ".gitattributes", ".editorconfig"}
# Textos de licencia, que REUSE no exige anotar.
LICENSE_FILES = re.compile(r"^(LICENSE|LICENSES/.*)$")
# Carpetas que no forman parte del código propio.
IGNORED_DIRS = ("build/", "out/", ".git/", ".vcpkg/", ".vcpkg-cache/", ".ndk/", "vcpkg_installed/")
PRIVATE_DIR = "content/official/"
# Código que ScanCode revisa, y ficheros que nombran licencias sin estar bajo ellas.
CODE_SUFFIXES = {".c", ".h", ".cpp", ".hpp", ".cmake", ".py", ".sh", ".map"}
NAMES_LICENSES = {"tools/ci/check_licenses.py"}


def project_files() -> list[str]:
    """Ficheros del repositorio: los de git si hay repositorio; si no, los del disco."""
    if (ROOT / ".git").exists():
        listed = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, check=True, capture_output=True).stdout
        return sorted(name for name in listed.decode("utf-8").split("\0") if name)
    files = []
    for path in ROOT.rglob("*"):
        relative = path.relative_to(ROOT).as_posix()
        if path.is_file() and not relative.startswith(IGNORED_DIRS):
            files.append(relative)
    return sorted(files)


def glob_to_regex(pattern: str) -> re.Pattern[str]:
    """Glob de REUSE.toml: '*' no cruza carpetas y '**' sí."""
    parts = re.split(r"(\*\*|\*)", pattern)
    regex = "".join(".*" if part == "**" else "[^/]*" if part == "*" else re.escape(part) for part in parts)
    return re.compile(f"^{regex}$")


def reuse_annotations() -> list[re.Pattern[str]]:
    reuse = ROOT / "REUSE.toml"
    if not reuse.exists():
        return []
    data = tomllib.loads(reuse.read_text(encoding="utf-8"))
    patterns = []
    for annotation in data.get("annotations", []):
        if annotation.get("SPDX-License-Identifier") != LICENSE_ID:
            continue
        paths = annotation.get("path", [])
        for pattern in [paths] if isinstance(paths, str) else paths:
            patterns.append(glob_to_regex(pattern))
    return patterns


def check_headers() -> list[str]:
    errors = []
    annotated = reuse_annotations()
    for name in project_files():
        if LICENSE_FILES.match(name) or (name.startswith(PRIVATE_DIR) and name != PRIVATE_DIR + "README.md"):
            continue
        if any(pattern.match(name) for pattern in annotated):
            continue
        path = ROOT / name
        if path.suffix not in HEADER_SUFFIXES and path.name not in HEADER_NAMES:
            errors.append(f"{name}: sin licencia declarada; añade una cabecera SPDX o cúbrelo en REUSE.toml")
            continue
        head = "\n".join(path.read_text(encoding="utf-8", errors="replace").splitlines()[:20])
        if f"SPDX-License-Identifier: {LICENSE_ID}" not in head:
            errors.append(f"{name}: falta 'SPDX-License-Identifier: {LICENSE_ID}' al principio")
        elif "SPDX-FileCopyrightText:" not in head:
            errors.append(f"{name}: falta 'SPDX-FileCopyrightText:' al principio")
    return errors


def expression_allowed(expression: str) -> bool:
    """Evalúa una expresión SPDX sencilla: con OR basta una opción; con AND, todas."""
    cleaned = re.sub(r"\s+WITH\s+[\w.+-]+", "", expression).replace("(", " ").replace(")", " ")
    if " OR " in cleaned:
        return any(expression_allowed(option) for option in cleaned.split(" OR "))
    licenses = [item.strip() for item in cleaned.split(" AND ") if item.strip()]
    return bool(licenses) and all(item in ALLOWED for item in licenses)


def check_vcpkg(installed: Path) -> list[str]:
    errors = []
    sboms = sorted(installed.glob("*/share/*/vcpkg.spdx.json"))
    if not sboms:
        return [f"{installed}: no hay ningún SBOM de vcpkg (vcpkg.spdx.json)"]
    for sbom in sboms:
        data = json.loads(sbom.read_text(encoding="utf-8"))
        package = data.get("packages", [{}])[0]
        name = package.get("name", sbom.parent.name)
        license_expression = package.get("licenseConcluded") or "NOASSERTION"
        if license_expression == "NOASSERTION":
            errors.append(f"vcpkg {name}: el SBOM no declara licencia")
        elif not expression_allowed(license_expression):
            errors.append(f"vcpkg {name}: licencia no permitida ({license_expression})")
        else:
            print(f"OK    vcpkg {name}: {license_expression}")
    return errors


def is_code(relative: str) -> bool:
    """Solo se revisa el código: la documentación y este script nombran licencias a propósito."""
    path = Path(relative)
    if relative in NAMES_LICENSES or path.suffix in {".md", ".toml", ".yaml", ".yml"}:
        return False
    return path.suffix in CODE_SUFFIXES or path.name == "CMakeLists.txt"


def check_scancode(report: Path) -> list[str]:
    errors = []
    checked = 0
    data = json.loads(report.read_text(encoding="utf-8"))
    for entry in data.get("files", []):
        if entry.get("type") != "file":
            continue
        path = entry.get("path", "")
        # ScanCode antepone el nombre de la carpeta analizada a cada ruta.
        relative = path.split("/", 1)[1] if "/" in path else path
        if relative.startswith(IGNORED_DIRS) or relative.startswith(PRIVATE_DIR) or not is_code(relative):
            continue
        checked += 1
        expression = entry.get("detected_license_expression_spdx") or ""
        if FORBIDDEN.search(expression):
            errors.append(f"{relative}: ScanCode detecta {expression}")
    print(f"ScanCode: {checked} ficheros de código revisados")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--vcpkg-installed", type=Path, help="carpeta vcpkg_installed de una compilación")
    parser.add_argument("--scancode", type=Path, help="informe JSON de ScanCode Toolkit")
    args = parser.parse_args()

    errors = check_headers()
    if args.vcpkg_installed:
        errors += check_vcpkg(args.vcpkg_installed)
    if args.scancode:
        errors += check_scancode(args.scancode)

    for error in errors:
        print(f"ERROR {error}")
    if errors:
        print(f"{len(errors)} problemas de licencia")
        return 1
    print("Licencias correctas")
    return 0


if __name__ == "__main__":
    sys.exit(main())
