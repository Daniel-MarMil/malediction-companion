# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
"""The import, step by step. Nothing is written to content/official/ unless every step passes.

1. robots.txt and card-sitemap.xml; each card page, from the cache when its lastmod is the same.
2. Each page is read (card_page); pages that fail are listed and nothing is written.
3. Stable ids (ids), then the names are checked with import_deck: if two cards cannot be told
   apart, the import stops.
4. The manual file with the Seeker-Legacy pairs and the starting echo (manual).
5. The official decks in content/official/decks/*.txt, read with import_deck and checked with
   validate_deck on the new pack.
6. The images, from the cache when their URL is the same.
7. The pack is written only after the core accepts it; then the report.
"""

from __future__ import annotations

import hashlib
import json
import re
import sys
import tempfile
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

from .cache import Cache, write_atomically
from .card_page import CardPage, parse_card_page
from .core import Core, find_mc_cli
from .errors import FetchError, ImportStopped, PageFormatError
from .fetch import SITE, Fetcher
from .ids import IdRegistry
from .manual import FILE_NAME as MANUAL_FILE
from .manual import Manual, load_manual
from .pack import ImageFile, build_pack, diff_report, faction_report, serialize
from .sitemap import parse_sitemap

SITEMAP_URL = SITE + "/card-sitemap.xml"
SITEMAP_MAX_BYTES = 5 * 1024 * 1024
PAGE_MAX_BYTES = 4 * 1024 * 1024
IMAGE_MAX_BYTES = 20 * 1024 * 1024
DECK_MAX_BYTES = 64 * 1024
PACK_FILE = "rules-pack.json"
IDS_FILE = "card-ids.json"
DECKS_DIR = "decks"
IMAGES_DIR = "images"
DECK_ID = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
IMAGE_TYPES = {".jpg": (b"\xff\xd8\xff",), ".jpeg": (b"\xff\xd8\xff",), ".png": (b"\x89PNG\r\n\x1a\n",), ".webp": (b"RIFF",)}


@dataclass
class Outcome:
    """What the import did, for the report and for the tests."""

    written: bool
    report: list[str]


def _say(message: str) -> None:
    print(message, file=sys.stderr, flush=True)


class Importer:
    def __init__(
        self,
        fetcher: Fetcher,
        cache: Cache,
        output: Path,
        mc_cli: Path,
        progress: Callable[[str], None] = _say,
    ) -> None:
        self.fetcher = fetcher
        self.cache = cache
        self.output = output
        self.mc_cli = mc_cli
        self.progress = progress
        self.downloads = 0

    # Step 1 and 2 -------------------------------------------------------------------------
    def _download(self, url: str, max_bytes: int) -> bytes:
        self.downloads += 1
        return self.fetcher.get(url, max_bytes)

    def read_pages(self) -> tuple[list[CardPage], list[str | None], list[str]]:
        try:
            entries = parse_sitemap(self._download(SITEMAP_URL, SITEMAP_MAX_BYTES))
        except FetchError as error:
            raise ImportStopped(f"cannot read the sitemap: {error}") from error
        self.progress(f"Sitemap: {len(entries)} card pages")
        pages: list[CardPage] = []
        failures: list[str] = []
        for number, entry in enumerate(entries, 1):
            body = self.cache.page(entry.url, entry.lastmod)
            if body is None:
                self.progress(f"[{number}/{len(entries)}] {entry.url}")
                try:
                    body = self._download(entry.url, PAGE_MAX_BYTES)
                except FetchError as error:
                    failures.append(f"{entry.url}: {error}")
                    continue
                self.cache.store_page(entry.url, entry.lastmod, body)
            try:
                pages.append(parse_card_page(body, entry.url))
            except (PageFormatError, UnicodeDecodeError) as error:
                failures.append(f"{entry.url}: {error}")
        return pages, [entry.lastmod for entry in entries], failures

    # Step 3 -------------------------------------------------------------------------------
    def check_names(self, pack: dict, pages: list[CardPage], ids: dict[str, str]) -> None:
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / PACK_FILE
            path.write_bytes(serialize(pack))
            problems = Core(self.mc_cli, path).unreadable_names([card["name"] for card in pack["cards"]])
        if not problems:
            return
        urls = {ids[page.url]: page.url for page in pages}
        groups: dict[str, list[str]] = {}
        for card in pack["cards"]:
            if card["name"] in problems:
                key = re.sub(r"[ \t ]+", " ", card["name"].replace("’", "'").replace("‘", "'"))
                error = problems[card["name"]]
                groups.setdefault(f"{error}: {key.lower()}", []).append(
                    f"{card['name']!r} ({card['id']}, {urls.get(card['id'])})"
                )
        lines = [f"{key.split(':')[0]}: " + "; ".join(names) for key, names in sorted(groups.items())]
        raise ImportStopped(
            "import_deck cannot tell these cards apart (or find them) by name, so export_deck could not "
            "write them; nothing was written:\n  " + "\n  ".join(lines)
        )

    # Step 5 -------------------------------------------------------------------------------
    def read_decks(self, pack: dict, manual: Manual) -> tuple[list[dict], list[str]]:
        folder = self.output / DECKS_DIR
        files = sorted(folder.glob("*.txt")) if folder.is_dir() else []
        names = {card["id"]: card["name"] for card in pack["cards"]}
        decks: list[dict] = []
        problems: list[str] = []
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / PACK_FILE
            path.write_bytes(serialize(pack))
            core = Core(self.mc_cli, path)
            for file in files:
                deck_id = file.stem
                if not DECK_ID.match(deck_id) or len(deck_id) > 64:
                    problems.append(f"{file.name}: the file name must be lowercase letters, digits and '-'")
                    continue
                data = file.read_bytes()
                if len(data) > DECK_MAX_BYTES:
                    problems.append(f"{file.name}: larger than {DECK_MAX_BYTES} bytes")
                    continue
                deck, deck_problems = self._read_deck(core, data.decode("utf-8", errors="replace"), manual, names)
                problems += [f"{file.name}: {problem}" for problem in deck_problems]
                if deck is None:
                    continue
                meta = manual.decks.get(deck_id, {})
                entry = {"id": deck_id, "name": meta.get("name") or names[deck["seeker"]]}
                if meta.get("url"):
                    entry["url"] = meta["url"]
                entry["deck"] = deck
                decks.append(entry)
        unused = sorted(set(manual.decks) - {file.stem for file in files})
        problems += [f"{MANUAL_FILE}: the deck {deck_id!r} has no file {DECKS_DIR}/{deck_id}.txt" for deck_id in unused]
        return decks, problems

    @staticmethod
    def _read_deck(core: Core, text: str, manual: Manual, names: dict[str, str]) -> tuple[dict | None, list[str]]:
        reply = core.run({"cmd": "import_deck", "text": text})
        if reply.get("ok") is not True:
            return None, [f"import_deck refused it: {reply.get('error')}: {reply.get('detail')}"]
        problems = [f"line {e.get('line')}: {e.get('error')}: {e.get('detail')}" for e in reply.get("errors", [])]
        deck = reply["deck"]
        for part in ("seeker", "legacy", "terrain"):
            if deck.get(part) is None:
                problems.append(f"it has no {part}")
        legacy = deck.get("legacy")
        if legacy in manual.legacies and deck.get("seeker") is not None:
            paired = manual.legacies[legacy].seeker
            if paired != deck["seeker"]:
                problems.append(
                    f"its Legacy {names.get(legacy)!r} goes with {names.get(paired)!r} in {MANUAL_FILE}, "
                    f"not with its Seeker {names.get(deck['seeker'])!r}"
                )
        if problems:
            return None, problems
        result = core.run({"cmd": "validate_deck", "deck": deck})
        if result.get("valid") is not True:
            issues = result.get("issues", [])
            return None, [
                f"validate_deck: {i.get('rule')}" + (f" ({i['card']})" if i.get("card") else "") + f": {i.get('detail')}"
                for i in issues
            ] or [f"validate_deck did not accept it: {result}"]
        return deck, []

    # Step 6 -------------------------------------------------------------------------------
    def read_images(self, pages: list[CardPage], ids: dict[str, str]) -> tuple[dict[str, tuple[ImageFile, bytes]], list[str]]:
        images: dict[str, tuple[ImageFile, bytes]] = {}
        failures: list[str] = []
        for page in sorted(pages, key=lambda p: ids[p.url]):
            card_id = ids[page.url]
            body = self.cache.image(page.image_url)
            if body is None:
                self.progress(f"Image of {card_id}: {page.image_url}")
                try:
                    body = self._download(page.image_url, IMAGE_MAX_BYTES)
                except FetchError as error:
                    failures.append(f"{page.url}: image: {error}")
                    continue
                suffix = Path(page.image_url).suffix.lower()
                if not body.startswith(IMAGE_TYPES[suffix]) or (suffix == ".webp" and body[8:12] != b"WEBP"):
                    failures.append(f"{page.url}: the image is not a {suffix} file: {page.image_url}")
                    continue
                self.cache.store_image(page.image_url, body)
            suffix = Path(page.image_url).suffix.lower().replace(".jpeg", ".jpg")
            image = ImageFile(f"{IMAGES_DIR}/{card_id}{suffix}", hashlib.sha256(body).hexdigest(), page.image_url)
            images[card_id] = (image, body)
        return images, failures

    # Step 7 -------------------------------------------------------------------------------
    def write(self, pack: dict, images: dict[str, tuple[ImageFile, bytes]], registry: IdRegistry) -> bool:
        """Writes the pack, its images and the ids. Returns whether the pack bytes changed."""
        data = serialize(pack)
        staged = self.output / (PACK_FILE + ".new")
        write_atomically(staged, data)
        try:
            info = Core(self.mc_cli, staged).run({"cmd": "pack_info"})
        except ImportStopped:
            staged.unlink(missing_ok=True)
            raise
        if not isinstance(info.get("pack"), dict) or info["pack"].get("cards") != len(pack["cards"]):
            staged.unlink(missing_ok=True)
            raise ImportStopped(f"the core read the new pack differently: {info}")

        folder = self.output / IMAGES_DIR
        folder.mkdir(parents=True, exist_ok=True)
        wanted = {Path(image.file).name: body for image, body in images.values()}
        for name, body in sorted(wanted.items()):
            path = folder / name
            if not path.exists() or path.read_bytes() != body:
                write_atomically(path, body)
        for path in sorted(folder.iterdir()):
            if path.is_file() and path.name not in wanted:
                path.unlink()

        target = self.output / PACK_FILE
        changed = not target.exists() or target.read_bytes() != data
        staged.replace(target)
        write_atomically(self.output / IDS_FILE, registry.to_bytes())
        return changed

    # The whole import -----------------------------------------------------------------------
    def run(self) -> Outcome:
        self.output.mkdir(parents=True, exist_ok=True)
        previous = self._previous_pack()
        if previous is not None and not (self.output / IDS_FILE).exists():
            raise ImportStopped(
                f"{self.output / PACK_FILE} exists but {IDS_FILE} does not; without it the card ids "
                "could change. Restore it from a backup before importing."
            )
        registry = IdRegistry.load(self.output / IDS_FILE)

        pages, lastmods, failures = self.read_pages()
        if failures:
            return Outcome(False, ["Pages that could not be read; nothing was written:"] + [f"  {f}" for f in failures])
        ids, notes = registry.assign(pages)
        self.check_names(build_pack(pages, ids, lastmods, None, {}, []), pages, ids)

        seekers = {ids[p.url]: p.name for p in pages if p.card_type == "seeker"}
        legacies = {ids[p.url]: p.name for p in pages if p.card_type == "legacy"}
        manual = load_manual(self.output / MANUAL_FILE, seekers, legacies)
        decks, deck_problems = self.read_decks(build_pack(pages, ids, lastmods, manual, {}, []), manual)
        images, image_failures = self.read_images(pages, ids)
        if deck_problems or image_failures:
            report = ["Nothing was written."]
            if deck_problems:
                report += ["Official decks that do not pass:"] + [f"  {p}" for p in deck_problems]
            if image_failures:
                report += ["Images that could not be read:"] + [f"  {f}" for f in image_failures]
            return Outcome(False, report)

        pack = build_pack(pages, ids, lastmods, manual, {key: value[0] for key, value in images.items()}, decks)
        changed = self.write(pack, images, registry)
        report = [f"Pack {pack['version']}: {len(pack['cards'])} cards, {len(pack['decks'])} official decks."]
        report.append(f"Downloads in this run: {self.downloads} (the rest came from the cache).")
        report += notes
        report += diff_report(previous, pack)
        report += faction_report(pack)
        if changed and (self.output / (PACK_FILE + ".sig")).exists():
            report.append(f"The pack changed: its signature {PACK_FILE}.sig no longer matches; sign it again.")
        return Outcome(True, report)

    def _previous_pack(self) -> dict | None:
        path = self.output / PACK_FILE
        if not path.exists():
            return None
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            raise ImportStopped(f"{path}: the previous pack cannot be read: {error}") from error
        return data if isinstance(data, dict) else None
