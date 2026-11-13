# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
"""The core, through mc_cli: the importer asks the same code as the apps (ADR-11).

- Card names that import_deck cannot tell apart are found with import_deck itself, not with a
  copy of its comparison rules, so this check cannot drift from the core.
- Official decks are read with import_deck and checked with validate_deck.
"""

from __future__ import annotations

import json
import os
import subprocess
from pathlib import Path

from .errors import ImportStopped

# import_deck reports at most 100 errors, so the names go in groups of at most 100 lines.
NAMES_PER_CALL = 100
TIMEOUT = 60


def find_mc_cli(explicit: str | None) -> Path:
    """mc_cli from --mc-cli, $MC_CLI, or the release or asan build under $MC_BUILD_ROOT."""
    candidates = [explicit] if explicit else []
    if os.environ.get("MC_CLI"):
        candidates.append(os.environ["MC_CLI"])
    build_root = Path(os.environ.get("MC_BUILD_ROOT", Path.home() / ".cache" / "seekers-companion" / "build"))
    candidates += [str(build_root / preset / "examples" / "mc_cli") for preset in ("release", "asan")]
    for candidate in candidates:
        path = Path(candidate)
        if path.is_file() and os.access(path, os.X_OK):
            return path
        if explicit and candidate == explicit:
            break
    raise ImportStopped(
        "mc_cli not found; build the core first (tools/ci/ci.sh release) or give its path with --mc-cli"
    )


class Core:
    def __init__(self, mc_cli: Path, pack_path: Path) -> None:
        self._mc_cli = mc_cli
        self._pack_path = pack_path

    def run(self, command: dict) -> dict:
        """Runs a command on the pack and returns the reply. Stops if the core rejects the pack."""
        try:
            result = subprocess.run(
                [str(self._mc_cli), str(self._pack_path), "-"],
                input=json.dumps(command, ensure_ascii=False).encode("utf-8"),
                capture_output=True,
                timeout=TIMEOUT,
                check=False,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise ImportStopped(f"mc_cli failed: {error}") from error
        stdout = result.stdout.decode("utf-8", errors="replace")
        if "rejected the pack" in result.stderr.decode("utf-8", errors="replace"):
            raise ImportStopped(f"the core rejects the pack: {stdout.strip()}")
        try:
            reply = json.loads(stdout)
        except ValueError as error:
            raise ImportStopped(f"mc_cli did not answer JSON (exit {result.returncode}): {stdout[:300]!r}") from error
        if not isinstance(reply, dict):
            raise ImportStopped(f"mc_cli answered something that is not an object: {stdout[:300]!r}")
        return reply

    def unreadable_names(self, names: list[str]) -> dict[str, str]:
        """The names that import_deck cannot read back, with its error (ambiguous_card, unknown_card...)."""
        problems: dict[str, str] = {}
        for start in range(0, len(names), NAMES_PER_CALL):
            group = names[start : start + NAMES_PER_CALL]
            text = "Main Deck\n" + "\n".join(f"1x {name}" for name in group)
            reply = self.run({"cmd": "import_deck", "text": text})
            if reply.get("ok") is not True:
                raise ImportStopped(f"import_deck failed: {reply}")
            for error in reply.get("errors", []):
                line = error.get("line")
                if not isinstance(line, int) or not 2 <= line <= len(group) + 1:
                    raise ImportStopped(f"import_deck reported an unexpected error: {error}")
                problems[group[line - 2]] = str(error.get("error"))
        return problems
