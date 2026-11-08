#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
"""Checks that ELF libraries are aligned to 16 KB pages (RNF-20).

Google Play requires apps that target Android 15 (API 35) or later to work with 16 KB
pages on 64-bit devices: every LOAD segment of their native libraries must be aligned to
16384 bytes (2**14) or more. The script reads the ELF headers directly, without NDK tools,
so it works the same on Linux, macOS and Windows.

Usage: check_page_alignment.py PATH [PATH ...]   (.so files or folders that contain them)
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

PAGE_SIZE = 16 * 1024
PT_LOAD = 1


def load_alignments(path: Path) -> list[int]:
    """Returns the alignment of every LOAD segment of an ELF file."""
    data = path.read_bytes()
    if data[:4] != b"\x7fELF":
        raise ValueError("not an ELF file")
    elf_class, byte_order = data[4], data[5]
    if elf_class not in (1, 2) or byte_order not in (1, 2):
        raise ValueError("unknown ELF header")
    endian = "<" if byte_order == 1 else ">"
    if elf_class == 2:  # 64-bit ELF
        phoff, = struct.unpack_from(endian + "Q", data, 0x20)
        phentsize, phnum = struct.unpack_from(endian + "HH", data, 0x36)
        header, align_offset = endian + "IIQQQQQQ", 7
    else:  # 32-bit ELF
        phoff, = struct.unpack_from(endian + "I", data, 0x1C)
        phentsize, phnum = struct.unpack_from(endian + "HH", data, 0x2A)
        header, align_offset = endian + "IIIIIIII", 7
    alignments = []
    for index in range(phnum):
        fields = struct.unpack_from(header, data, phoff + index * phentsize)
        if fields[0] == PT_LOAD:
            alignments.append(fields[align_offset])
    return alignments


def libraries(paths: list[str]) -> list[Path]:
    found: list[Path] = []
    for raw in paths:
        path = Path(raw)
        found.extend(sorted(path.rglob("*.so")) if path.is_dir() else [path])
    return found


def main(argv: list[str]) -> int:
    if not argv:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    targets = libraries(argv)
    if not targets:
        print("No .so library found", file=sys.stderr)
        return 1
    failed = False
    for library in targets:
        try:
            alignments = load_alignments(library)
        except (OSError, ValueError, struct.error) as error:
            print(f"ERROR {library}: {error}")
            failed = True
            continue
        bad = [a for a in alignments if a < PAGE_SIZE or a & (a - 1)]
        if not alignments or bad:
            shown = ", ".join(hex(a) for a in alignments) or "none"
            print(f"FAIL  {library}: LOAD segments aligned to {shown}; {hex(PAGE_SIZE)} or more is required")
            failed = True
        else:
            print(f"OK    {library}: {len(alignments)} LOAD segments aligned to {hex(min(alignments))} or more")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
