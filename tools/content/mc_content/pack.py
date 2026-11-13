# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
"""Builds the official pack in format 2 and compares it with the previous one.

The output is deterministic: the same pages, images, manual file and decks give the same bytes.
Cards, factions and decks are sorted by id, the date is the newest lastmod of the sitemap, and
the version is that date plus the start of a SHA-256 of the rest of the pack.
"""

from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass

from .card_page import CardPage
from .errors import ImportStopped
from .manual import Manual

FORMAT = "mc-rules-pack"
FORMAT_VERSION = 2
PACK_ID = "malediction-official"
ORIGIN = "malediction.gg card library (https://malediction.gg/content-library/), read by tools/content"
LICENSE = "Content of Loot Studios, all rights reserved; for development only, never distributed (ADR-09 and ADR-12)"
# Deck rules of section 3 of the requirements document, as in the test pack. They are data of
# the pack (ADR-06); the web pages do not give them.
DECK_RULES = {
    "deck_min": 30,
    "deck_max": 50,
    "side_max": 7,
    "copy_limits": {"basic": 3, "elite": 2, "unique": 1},
    "unaligned_cards_allowed": True,
    "copy_limits_include_side": True,
}
DATE = re.compile(r"^\d{4}-\d{2}-\d{2}")


@dataclass(frozen=True)
class ImageFile:
    file: str  # Relative to the pack: images/<id><extension>
    sha256: str
    source: str


def factions_of(pages: list[CardPage]) -> list[dict[str, str]]:
    """Every faction of the pages, sorted by id. Stops if one id has two names."""
    names: dict[str, str] = {}
    for page in pages:
        for faction_id, name in page.factions:
            if names.setdefault(faction_id, name) != name:
                raise ImportStopped(f"the faction {faction_id!r} is called both {names[faction_id]!r} and {name!r}")
    return [{"id": faction_id, "name": names[faction_id]} for faction_id in sorted(names)]


def card_json(card_id: str, page: CardPage, manual: Manual | str, image: ImageFile | None) -> dict:
    """manual is the manual file, or the id of a Seeker to stand in for it in a provisional pack."""
    card: dict = {"id": card_id, "name": page.name, "type": page.card_type}
    if page.rank is not None:
        card["rank"] = page.rank
    card["sigils"] = sorted(faction_id for faction_id, _ in page.factions)
    if page.card_type == "legacy":
        if isinstance(manual, Manual):
            card["seeker"] = manual.legacies[card_id].seeker
            card["starting_echo"] = manual.legacies[card_id].starting_echo
        else:
            card["seeker"] = manual
            card["starting_echo"] = 0
    card["cost"] = None  # The pages do not give it yet.
    card["traits"] = list(page.traits)
    card["abilities"] = list(page.abilities)
    if page.stats is not None:
        card["stats"] = dict(page.stats)
    card["rules_text"] = None  # Only on the card image; it will be transcribed later.
    if image is not None:
        card["image"] = {"file": image.file, "sha256": image.sha256, "source": image.source}
    card["source"] = page.url
    return card


def serialize(pack: dict) -> bytes:
    return (json.dumps(pack, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def build_pack(
    pages: list[CardPage],
    ids: dict[str, str],
    lastmods: list[str | None],
    manual: Manual | None,
    images: dict[str, ImageFile],
    decks: list[dict],
) -> dict:
    """The pack as a JSON object.

    manual is None only for the provisional pack that checks the names before the manual file is
    read: there every Legacy points to the first Seeker, so that the core accepts the pack.
    """
    dates = sorted(value[:10] for value in lastmods if value and DATE.match(value))
    if not dates:
        raise ImportStopped("the sitemap gives no lastmod, so the pack has no date")
    stand_in: Manual | str
    if manual is None:
        seekers = sorted(ids[page.url] for page in pages if page.card_type == "seeker")
        if not seekers:
            raise ImportStopped("the pages have no Seeker")
        stand_in = seekers[0]
    else:
        stand_in = manual
    cards = sorted(
        (card_json(ids[page.url], page, stand_in, images.get(ids[page.url])) for page in pages),
        key=lambda card: card["id"],
    )
    pack = {
        "format": FORMAT,
        "format_version": FORMAT_VERSION,
        "id": PACK_ID,
        "version": "",
        "date": dates[-1],
        "origin": ORIGIN,
        "license": LICENSE,
        "rules": DECK_RULES,
        "factions": factions_of(pages),
        "cards": cards,
        "decks": sorted(decks, key=lambda deck: deck["id"]),
    }
    digest = hashlib.sha256(serialize(pack)).hexdigest()[:12]
    pack["version"] = f"{dates[-1]}+{digest}"
    return pack


def _short(value: object) -> str:
    text = json.dumps(value, ensure_ascii=False, sort_keys=True)
    return text if len(text) <= 80 else text[:77] + "..."


def _by_id(items: object) -> dict[str, dict]:
    if not isinstance(items, list):
        return {}
    return {item["id"]: item for item in items if isinstance(item, dict) and isinstance(item.get("id"), str)}


def _diff_items(kind: str, old: dict[str, dict], new: dict[str, dict]) -> list[str]:
    lines = []
    added = sorted(set(new) - set(old))
    removed = sorted(set(old) - set(new))
    changed = sorted(key for key in set(old) & set(new) if old[key] != new[key])
    lines.append(f"{kind}: {len(added)} new, {len(removed)} removed, {len(changed)} changed")
    lines += [f"  + {key} ({new[key].get('name', '')})" for key in added]
    lines += [f"  - {key} ({old[key].get('name', '')})" for key in removed]
    for key in changed:
        lines.append(f"  * {key}")
        for field in sorted(set(old[key]) | set(new[key])):
            before, after = old[key].get(field), new[key].get(field)
            if before != after:
                lines.append(f"      {field}: {_short(before)} -> {_short(after)}")
    return lines


def diff_report(previous: dict | None, pack: dict) -> list[str]:
    """Changes against the previous pack: new, removed and changed cards, factions and decks."""
    if previous is None:
        return [f"No previous pack: {len(pack['cards'])} cards, {len(pack['factions'])} factions, "
                f"{len(pack['decks'])} official decks."]
    lines = [f"Previous pack: version {previous.get('version')!r}, format {previous.get('format_version')!r}."]
    for kind in ("factions", "cards", "decks"):
        lines += _diff_items(kind.capitalize(), _by_id(previous.get(kind)), _by_id(pack[kind]))
    return lines


def faction_report(pack: dict) -> list[str]:
    """Whether the factions of the pages are enough for the sigil rule of RF-06 (dual cards)."""
    seekers = [card for card in pack["cards"] if card["type"] == "seeker"]
    seeker_sets = {tuple(card["sigils"]) for card in seekers}
    duals = [card for card in pack["cards"] if len(card["sigils"]) >= 2 and card["type"] != "seeker"]
    multi = [card for card in seekers if len(card["sigils"]) >= 2]
    lines = [
        f"Sigils (RF-06): {len(seekers)} Seekers, {len(multi)} of them with two or more factions; "
        f"{len(duals)} other cards with two or more factions.",
    ]
    lines += [f"  Seeker {card['id']}: {card['sigils']}" for card in multi]
    lines += [f"  Card {card['id']}: {card['sigils']}" for card in duals]
    orphans = [card for card in duals if not any(set(card["sigils"]) <= set(s) for s in seeker_sets)]
    if orphans:
        lines.append("  Dual cards that no Seeker can take: " + ", ".join(f"{c['id']} {c['sigils']}" for c in orphans))
    if any(not card["sigils"] for card in seekers):
        lines.append("  A Seeker without a faction could only take unaligned cards: the pages are not enough.")
    return lines
