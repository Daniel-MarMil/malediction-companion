#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
"""Comprueba que las bibliotecas ELF están alineadas a páginas de 16 KB (RNF-20).

Google Play exige que las apps que se dirigen a Android 15 (API 35) o posterior funcionen
con páginas de 16 KB en dispositivos de 64 bits: cada segmento LOAD de sus bibliotecas
nativas debe estar alineado a 16384 bytes (2**14) o más. Lee las cabeceras ELF directamente, sin herramientas del NDK, para que
funcione igual en Linux, macOS y Windows.

Uso: check_page_alignment.py RUTA [RUTA ...]   (ficheros .so o carpetas que los contengan)
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

PAGE_SIZE = 16 * 1024
PT_LOAD = 1


def load_alignments(path: Path) -> list[int]:
    """Devuelve la alineación de cada segmento LOAD de un fichero ELF."""
    data = path.read_bytes()
    if data[:4] != b"\x7fELF":
        raise ValueError("no es un fichero ELF")
    elf_class, byte_order = data[4], data[5]
    if elf_class not in (1, 2) or byte_order not in (1, 2):
        raise ValueError("cabecera ELF desconocida")
    endian = "<" if byte_order == 1 else ">"
    if elf_class == 2:  # ELF de 64 bits
        phoff, = struct.unpack_from(endian + "Q", data, 0x20)
        phentsize, phnum = struct.unpack_from(endian + "HH", data, 0x36)
        header, align_offset = endian + "IIQQQQQQ", 7
    else:  # ELF de 32 bits
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
        print("No se ha encontrado ninguna biblioteca .so", file=sys.stderr)
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
            shown = ", ".join(hex(a) for a in alignments) or "ninguno"
            print(f"FALLA {library}: segmentos LOAD alineados a {shown}; hace falta {hex(PAGE_SIZE)} o más")
            failed = True
        else:
            print(f"OK    {library}: {len(alignments)} segmentos LOAD alineados a {hex(min(alignments))} o más")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
