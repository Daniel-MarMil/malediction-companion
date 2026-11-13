# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
"""Download cache, outside the repository (by default ~/.cache/seekers-companion/content/).

A page is downloaded again only when its lastmod in the sitemap changes; an image, only when
its URL changes. The indexes are saved after every download, so an interrupted import keeps
what it already has.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path

DEFAULT_CACHE = Path.home() / ".cache" / "seekers-companion" / "content"


def _key(url: str) -> str:
    return hashlib.sha256(url.encode("utf-8")).hexdigest()[:40]


def write_atomically(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(data)
    os.replace(temporary, path)


class Cache:
    def __init__(self, root: Path) -> None:
        self.root = root
        self._pages = self._load("pages.json")
        self._images = self._load("images.json")

    def _load(self, name: str) -> dict[str, dict[str, str]]:
        path = self.root / name
        if not path.exists():
            return {}
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return {}  # A damaged index only means downloading again.
        return data if isinstance(data, dict) else {}

    def _save(self, name: str, index: dict[str, dict[str, str]]) -> None:
        text = json.dumps(index, indent=1, sort_keys=True) + "\n"
        write_atomically(self.root / name, text.encode("utf-8"))

    def page(self, url: str, lastmod: str | None) -> bytes | None:
        """The cached page if its lastmod is the same; None if it must be downloaded."""
        entry = self._pages.get(url)
        if lastmod is None or entry is None or entry.get("lastmod") != lastmod:
            return None
        return self._read(entry)

    def store_page(self, url: str, lastmod: str | None, body: bytes) -> None:
        file = f"pages/{_key(url)}.html"
        write_atomically(self.root / file, body)
        self._pages[url] = {"lastmod": lastmod or "", "file": file, "sha256": hashlib.sha256(body).hexdigest()}
        self._save("pages.json", self._pages)

    def image(self, url: str) -> bytes | None:
        entry = self._images.get(url)
        return None if entry is None else self._read(entry)

    def store_image(self, url: str, body: bytes) -> None:
        suffix = Path(url).suffix.lower()
        file = f"images/{_key(url)}{suffix}"
        write_atomically(self.root / file, body)
        self._images[url] = {"file": file, "sha256": hashlib.sha256(body).hexdigest()}
        self._save("images.json", self._images)

    def _read(self, entry: dict[str, str]) -> bytes | None:
        try:
            body = (self.root / entry["file"]).read_bytes()
        except (OSError, KeyError):
            return None
        if hashlib.sha256(body).hexdigest() != entry.get("sha256"):
            return None  # Damaged or edited by hand: download it again.
        return body
