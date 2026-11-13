#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
"""Imports the official card pack from the public malediction.gg card library.

It writes content/official/rules-pack.json (format 2), content/official/images/ and
content/official/card-ids.json, which git ignores (ADR-12). The pack is signed afterwards by
hand with mc_pack_tool sign; the importer never signs.

Usage:
  tools/content/import_official.py [--output DIR] [--cache DIR] [--mc-cli PATH]

It needs mc_cli (tools/ci/ci.sh release builds it). Exit status: 0 when the pack was written,
1 when something must be fixed first (nothing was written) and 2 when the import stopped.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from mc_content.cache import DEFAULT_CACHE, Cache  # noqa: E402
from mc_content.core import find_mc_cli  # noqa: E402
from mc_content.errors import ImportStopped  # noqa: E402
from mc_content.fetch import Fetcher, UrllibTransport  # noqa: E402
from mc_content.importer import Importer  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, default=ROOT / "content" / "official", help="pack folder")
    parser.add_argument("--cache", type=Path, default=DEFAULT_CACHE, help="download cache, outside the repository")
    parser.add_argument("--mc-cli", help="path to mc_cli (default: $MC_CLI or the release or asan build)")
    args = parser.parse_args(argv)

    if args.cache.resolve().is_relative_to(ROOT):
        print("The cache must be outside the repository.", file=sys.stderr)
        return 2
    try:
        importer = Importer(
            Fetcher(UrllibTransport(), log=lambda message: print(message, file=sys.stderr, flush=True)),
            Cache(args.cache),
            args.output,
            find_mc_cli(args.mc_cli),
        )
        outcome = importer.run()
    except ImportStopped as error:
        print(f"Import stopped; nothing was written to the pack.\n{error}", file=sys.stderr)
        return 2
    print("\n".join(outcome.report))
    return 0 if outcome.written else 1


if __name__ == "__main__":
    sys.exit(main())
