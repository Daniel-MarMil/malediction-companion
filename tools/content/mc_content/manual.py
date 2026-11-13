# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
"""The manual file content/official/seekers-legacies.json: what the web does not give.

{
  "format": "mc-official-manual",
  "format_version": 1,
  "legacies": {
    "<Legacy id>": {"name": "<only to read it>", "seeker": "<Seeker id>", "starting_echo": 4}
  },
  "decks": {
    "<official deck file name without .txt>": {"name": "<deck name>", "url": "<Play Network page>"}
  },
  "seekers": {"<Seeker id>": "<name>"}
}

- "legacies" pairs each Legacy with its Seeker and gives its starting echo. Every Legacy of the
  pack needs an entry and every Seeker needs at least one Legacy; otherwise the import stops.
- "decks" is optional: the name and the Play Network link of each official deck. Without a
  name, a deck takes its Seeker's name (RF-10).
- "seekers" and the "name" of each Legacy are only there to help fill the file; they are not read.

If the file does not exist, the importer writes it with every Seeker and Legacy it found and the
values empty (null), and stops. If a new Legacy appears, it adds its entry the same way.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path

from .cache import write_atomically
from .errors import ImportStopped

FORMAT = "mc-official-manual"
FILE_NAME = "seekers-legacies.json"
MAX_ECHO = 1000


@dataclass(frozen=True)
class LegacyInfo:
    seeker: str
    starting_echo: int


@dataclass
class Manual:
    legacies: dict[str, LegacyInfo]
    decks: dict[str, dict[str, str]] = field(default_factory=dict)


def _dump(data: dict) -> bytes:
    return (json.dumps(data, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def load_manual(path: Path, seekers: dict[str, str], legacies: dict[str, str]) -> Manual:
    """Reads and checks the file. seekers and legacies map the ids of the pack to their names."""
    if not path.exists():
        template = {
            "format": FORMAT,
            "format_version": 1,
            "legacies": {
                legacy_id: {"name": legacies[legacy_id], "seeker": None, "starting_echo": None}
                for legacy_id in sorted(legacies)
            },
            "decks": {},
            "seekers": {seeker_id: seekers[seeker_id] for seeker_id in sorted(seekers)},
        }
        write_atomically(path, _dump(template))
        raise ImportStopped(
            f"{path} did not exist; it now lists the {len(seekers)} Seekers and {len(legacies)} Legacies "
            "found. Fill in the Seeker and the starting echo of each Legacy and run the import again."
        )
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ImportStopped(f"{path}: cannot read it: {error}") from error
    if not isinstance(data, dict) or data.get("format") != FORMAT or data.get("format_version") != 1:
        raise ImportStopped(f"{path}: not a {FORMAT} file, version 1")
    entries = data.get("legacies")
    if not isinstance(entries, dict):
        raise ImportStopped(f"{path}: 'legacies' must be an object")

    missing = sorted(set(legacies) - set(entries))
    if missing:
        for legacy_id in missing:
            entries[legacy_id] = {"name": legacies[legacy_id], "seeker": None, "starting_echo": None}
        write_atomically(path, _dump(data))
        raise ImportStopped(
            f"{path}: new Legacies without an entry; they were added with empty values, fill them in: "
            + ", ".join(missing)
        )

    problems: list[str] = []
    result: dict[str, LegacyInfo] = {}
    for legacy_id, entry in sorted(entries.items()):
        if legacy_id not in legacies:
            problems.append(f"{legacy_id}: not a Legacy of the pack")
            continue
        seeker = entry.get("seeker") if isinstance(entry, dict) else None
        echo = entry.get("starting_echo") if isinstance(entry, dict) else None
        if not isinstance(seeker, str) or seeker not in seekers:
            problems.append(f"{legacy_id}: 'seeker' must be the id of a Seeker of the pack, not {seeker!r}")
        if isinstance(echo, bool) or not isinstance(echo, int) or not 0 <= echo <= MAX_ECHO:
            problems.append(f"{legacy_id}: 'starting_echo' must be an integer from 0 to {MAX_ECHO}, not {echo!r}")
        if not any(p.startswith(legacy_id + ":") for p in problems):
            result[legacy_id] = LegacyInfo(seeker, echo)
    if not problems:  # With wrong entries, a Seeker without a Legacy would only repeat them.
        paired = {info.seeker for info in result.values()}
        problems.extend(f"the Seeker {seeker_id} has no Legacy" for seeker_id in sorted(set(seekers) - paired))

    decks = data.get("decks", {})
    if not isinstance(decks, dict) or not all(
        isinstance(meta, dict) and all(isinstance(meta.get(k, ""), str) for k in ("name", "url"))
        for meta in decks.values()
    ):
        problems.append("'decks' must map each deck to an object with an optional 'name' and 'url'")
        decks = {}
    if problems:
        raise ImportStopped(f"{path} is incomplete or wrong:\n  " + "\n  ".join(problems))
    return Manual(result, decks)
