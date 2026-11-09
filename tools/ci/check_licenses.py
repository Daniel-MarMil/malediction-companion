#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
"""Checks the project's licenses (ADR-08 and RNF-15).

1. Every file of our own carries the Apache-2.0 SPDX header, or REUSE.toml covers it.
2. With --vcpkg-installed: the vcpkg dependencies have allowed licenses, according to
   the SPDX SBOM that vcpkg generates for each package. The build tools in BUILD_TOOLS
   were reviewed by hand instead; they pass only while their copyright file is unchanged.
3. With --scancode: the ScanCode Toolkit JSON report finds no GPL, LGPL or AGPL
   in our own code.

Usage:
  check_licenses.py [--vcpkg-installed FOLDER] [--scancode REPORT.json]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import tomllib
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LICENSE_ID = "Apache-2.0"

# Licenses allowed for dependencies (ADR-08).
ALLOWED = {"Apache-2.0", "MIT", "BSD-2-Clause", "BSD-3-Clause", "ISC", "Zlib", "BSL-1.0", "CC0-1.0", "Unlicense", "0BSD"}
FORBIDDEN = re.compile(r"\b(A|L)?GPL", re.IGNORECASE)


@dataclass(frozen=True)
class BuildTool:
    """vcpkg port reviewed by hand: it is used only to build and is not distributed (ADR-08)."""

    license: str
    reason: str
    # SHA-256 of share/<port>/copyright when it was reviewed; any change needs a new review.
    copyright_sha256: str


# Explicit list, with no wildcards: a new build tool fails until someone reviews it.
BUILD_TOOLS = {
    "vcpkg-make": BuildTool(
        license="MIT (CMake scripts) and GPL-2.0-or-later (GNU Automake scripts)",
        reason="host dependency of libsodium, used only to build and not distributed",
        copyright_sha256="be3035bc85008f76b42ccb76677269fb1a6916d669abf2c79b63c604a4ef2365",
    ),
}

HEADER_SUFFIXES = {".c", ".h", ".cpp", ".hpp", ".cmake", ".py", ".sh", ".yaml", ".yml", ".toml", ".md", ".map"}
HEADER_NAMES = {"CMakeLists.txt", ".clang-format", ".clang-tidy", ".gitignore", ".gitattributes", ".editorconfig"}
# License texts, which REUSE does not require to be annotated.
LICENSE_FILES = re.compile(r"^(LICENSE|LICENSES/.*)$")
# Folders that are not part of our own code.
IGNORED_DIRS = ("build/", "out/", ".git/", ".vcpkg/", ".vcpkg-cache/", ".ndk/", "vcpkg_installed/")
PRIVATE_DIR = "content/official/"
# Code that ScanCode checks, and files that name licenses without being under them.
CODE_SUFFIXES = {".c", ".h", ".cpp", ".hpp", ".cmake", ".py", ".sh", ".map"}
NAMES_LICENSES = {"tools/ci/check_licenses.py"}


def project_files() -> list[str]:
    """Repository files: the ones git tracks if there is a repository; otherwise, the ones on disk."""
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
    """REUSE.toml glob: '*' does not cross folders and '**' does."""
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
            errors.append(f"{name}: no declared license; add an SPDX header or cover it in REUSE.toml")
            continue
        head = "\n".join(path.read_text(encoding="utf-8", errors="replace").splitlines()[:20])
        if f"SPDX-License-Identifier: {LICENSE_ID}" not in head:
            errors.append(f"{name}: missing 'SPDX-License-Identifier: {LICENSE_ID}' at the top")
        elif "SPDX-FileCopyrightText:" not in head:
            errors.append(f"{name}: missing 'SPDX-FileCopyrightText:' at the top")
    return errors


def expression_allowed(expression: str) -> bool:
    """Evaluates a simple SPDX expression: with OR one option is enough; with AND, all are needed."""
    cleaned = re.sub(r"\s+WITH\s+[\w.+-]+", "", expression).replace("(", " ").replace(")", " ")
    if " OR " in cleaned:
        return any(expression_allowed(option) for option in cleaned.split(" OR "))
    licenses = [item.strip() for item in cleaned.split(" AND ") if item.strip()]
    return bool(licenses) and all(item in ALLOWED for item in licenses)


def check_build_tool(name: str, tool: BuildTool, copyright_file: Path) -> list[str]:
    if not copyright_file.is_file():
        return [f"vcpkg {name}: {copyright_file} not found; review the build tool again"]
    digest = hashlib.sha256(copyright_file.read_bytes()).hexdigest()
    if digest != tool.copyright_sha256:
        return [
            f"vcpkg {name}: its copyright file changed (SHA-256 {digest}, reviewed "
            f"{tool.copyright_sha256}); review its license again and update BUILD_TOOLS"
        ]
    print(f"BUILD vcpkg {name}: {tool.license}; {tool.reason}")
    return []


def check_vcpkg(installed: Path) -> list[str]:
    errors = []
    sboms = sorted(installed.glob("*/share/*/vcpkg.spdx.json"))
    if not sboms:
        return [f"{installed}: no vcpkg SBOM found (vcpkg.spdx.json)"]
    for sbom in sboms:
        data = json.loads(sbom.read_text(encoding="utf-8"))
        package = data.get("packages", [{}])[0]
        name = package.get("name", sbom.parent.name)
        if name in BUILD_TOOLS:
            errors += check_build_tool(name, BUILD_TOOLS[name], sbom.parent / "copyright")
            continue
        license_expression = package.get("licenseConcluded") or "NOASSERTION"
        if license_expression == "NOASSERTION":
            errors.append(f"vcpkg {name}: the SBOM declares no license")
        elif not expression_allowed(license_expression):
            errors.append(f"vcpkg {name}: license not allowed ({license_expression})")
        else:
            print(f"OK    vcpkg {name}: {license_expression}")
    return errors


def is_code(relative: str) -> bool:
    """Only code is checked: the documentation and this script name licenses on purpose."""
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
        # ScanCode prefixes every path with the name of the scanned folder.
        relative = path.split("/", 1)[1] if "/" in path else path
        if relative.startswith(IGNORED_DIRS) or relative.startswith(PRIVATE_DIR) or not is_code(relative):
            continue
        checked += 1
        expression = entry.get("detected_license_expression_spdx") or ""
        if FORBIDDEN.search(expression):
            errors.append(f"{relative}: ScanCode detects {expression}")
    print(f"ScanCode: {checked} code files checked")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--vcpkg-installed", type=Path, help="vcpkg_installed folder of a build")
    parser.add_argument("--scancode", type=Path, help="ScanCode Toolkit JSON report")
    args = parser.parse_args()

    errors = check_headers()
    if args.vcpkg_installed:
        errors += check_vcpkg(args.vcpkg_installed)
    if args.scancode:
        errors += check_scancode(args.scancode)

    for error in errors:
        print(f"ERROR {error}")
    if errors:
        print(f"{len(errors)} license problems")
        return 1
    print("Licenses are correct")
    return 0


if __name__ == "__main__":
    sys.exit(main())
