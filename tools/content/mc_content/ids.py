# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
"""Stable card ids, kept in content/official/card-ids.json.

A card's id is the slug of its page the first time the importer sees it (for example
"ash-test-unit"): ASCII, unique, and independent of the order of the sitemap. From then on
the registry ties the id to the WordPress post id of the page (from its shortlink), not to the
slug. So:

- If the slug of a page changes, the post id is the same, the card keeps its id and the report
  says so.
- If a card disappears, its id stays in the registry and is never given to another card.
- If a new post has the slug or the name of a card in the registry (for example, the page was
  deleted and created again), the importer stops: it cannot know whether it is the same card.
  If it is, set that entry's post_id to the new post by hand; if not, add an entry with another
  id.

Decks saved on the server are checked against every published version of the pack (ADR-13),
so an id must never change or be reused.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path

from .card_page import CardPage
from .errors import ImportStopped

FORMAT = "mc-card-ids"
CARD_ID = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
MAX_ID_LENGTH = 64


@dataclass
class IdEntry:
    id: str
    post_id: int
    slug: str
    name: str


def slug_of(url: str) -> str:
    return url.rstrip("/").rsplit("/", 1)[-1]


class IdRegistry:
    def __init__(self, entries: list[IdEntry] | None = None) -> None:
        self.entries = entries or []

    @classmethod
    def load(cls, path: Path) -> IdRegistry:
        if not path.exists():
            return cls()
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            raise ImportStopped(f"{path}: cannot read it: {error}") from error
        if not isinstance(data, dict) or data.get("format") != FORMAT or data.get("format_version") != 1:
            raise ImportStopped(f"{path}: not a {FORMAT} file, version 1")
        entries = []
        for item in data.get("cards", []):
            try:
                entry = IdEntry(str(item["id"]), int(item["post_id"]), str(item["slug"]), str(item["name"]))
            except (KeyError, TypeError, ValueError) as error:
                raise ImportStopped(f"{path}: bad entry {item!r}") from error
            if not CARD_ID.match(entry.id) or len(entry.id) > MAX_ID_LENGTH:
                raise ImportStopped(f"{path}: bad id {entry.id!r}")
            entries.append(entry)
        for field in ("id", "post_id"):
            values = [getattr(entry, field) for entry in entries]
            if len(set(values)) != len(values):
                raise ImportStopped(f"{path}: two entries share the same {field}")
        return cls(entries)

    def to_bytes(self) -> bytes:
        cards = [
            {"id": e.id, "post_id": e.post_id, "slug": e.slug, "name": e.name}
            for e in sorted(self.entries, key=lambda entry: entry.id)
        ]
        data = {"format": FORMAT, "format_version": 1, "cards": cards}
        return (json.dumps(data, indent=2, ensure_ascii=False) + "\n").encode("utf-8")

    def assign(self, pages: list[CardPage]) -> tuple[dict[str, str], list[str]]:
        """The id of each page, by URL, and notes for the report. Updates the registry."""
        by_post = {entry.post_id: entry for entry in self.entries}
        seen_posts: dict[int, str] = {}
        ids: dict[str, str] = {}
        notes: list[str] = []
        conflicts: list[str] = []
        for page in sorted(pages, key=lambda p: p.url):
            slug = slug_of(page.url)
            if page.post_id in seen_posts:
                raise ImportStopped(f"{page.url} and {seen_posts[page.post_id]} have the same post id {page.post_id}")
            seen_posts[page.post_id] = page.url
            entry = by_post.get(page.post_id)
            if entry is not None:
                if entry.slug != slug:
                    notes.append(f"{entry.id}: the page moved from /card/{entry.slug}/ to /card/{slug}/; the id stays")
                    entry.slug = slug
                entry.name = page.name
                ids[page.url] = entry.id
                continue
            clash = [e for e in self.entries if e.slug == slug or e.name == page.name]
            if clash:
                names = ", ".join(f"{e.id} (post {e.post_id}, /card/{e.slug}/)" for e in clash)
                conflicts.append(f"{page.url} (post {page.post_id}, {page.name!r}) looks like {names}")
                continue
            new_id = self._new_id(slug)
            entry = IdEntry(new_id, page.post_id, slug, page.name)
            self.entries.append(entry)
            by_post[page.post_id] = entry
            ids[page.url] = new_id
        if conflicts:
            raise ImportStopped(
                "new pages look like cards already in card-ids.json; if one is the same card, set the "
                "post_id of its entry to the new post, and if not, add an entry with another id:\n  "
                + "\n  ".join(conflicts)
            )
        return ids, notes

    def _new_id(self, slug: str) -> str:
        base = slug[:MAX_ID_LENGTH].strip("-")
        if not CARD_ID.match(base):
            raise ImportStopped(f"cannot make an id from the slug {slug!r}")
        taken = {entry.id for entry in self.entries}
        candidate, number = base, 2
        while candidate in taken:
            suffix = f"-{number}"
            candidate = base[: MAX_ID_LENGTH - len(suffix)].rstrip("-") + suffix
            number += 1
        return candidate
