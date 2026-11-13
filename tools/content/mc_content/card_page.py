# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
"""Reads one card page of the malediction.gg library (/card/<slug>/).

The shape was checked on 2026-11-13 against a few real pages. The title is the h1 with the
class compTitle_item; next to it come sections, each a div whose first child is a heading
p.ttl-5.ark: "Faction" (links to /faction/<slug>/ with data-faction-short), "Stats" (h5
statusAttack, statusPowerHit, statusPowerGraze, statusRange, statusMovement, statusDefense
and statusHealth, each a number or a dash for a null stat), "Skills" and "Specifications" (chips span.comp-chip-label). The image is
img.cardMedia and the WordPress post id comes from <link rel="shortlink" href="...?p=N">.

Specifications hold the traits, then the type ("Unit", "Spell", "Legacy", "Terrain", "Item"
or "Relic", with "Seeker" before "Unit" on a Seeker) and, last, the rank ("Basic", "Elite",
"Unique", "Legendary", or "Relic" on relics). Anything else raises PageFormatError: the
importer does not guess.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from html.parser import HTMLParser
from urllib.parse import parse_qs, urlsplit

from .errors import PageFormatError

VOID_TAGS = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "source", "track", "wbr"}
SECTIONS = ("Faction", "Stats", "Skills", "Specifications")
STATS = {
    "statusAttack": "accuracy",
    "statusPowerHit": "power_hit",
    "statusPowerGraze": "power_graze",
    "statusRange": "range",
    "statusMovement": "speed",
    "statusDefense": "defense",
    "statusHealth": "max_health",
}
# The card's dash, a null stat (rulebook, July 2026, p. 10). The site writes "-" or "--"; the
# rulebook prints "–" (en dash). All three mean the same.
NULL_STATS = {"-", "--", "\u2013"}
TYPES = {"Unit": "unit", "Spell": "spell", "Legacy": "legacy", "Terrain": "terrain", "Item": "item", "Relic": "relic"}
RANKS = {"Basic": "basic", "Elite": "elite", "Unique": "unique", "Legendary": "legendary", "Relic": None}
UNIT_TYPES = {"unit", "seeker"}
IMAGE_URL = re.compile(r"^https://malediction\.gg/wp-content/uploads/[A-Za-z0-9_./-]+\.(jpe?g|png|webp)$")
FACTION_URL = re.compile(r"^https://malediction\.gg/faction/[a-z0-9-]+/$")
FACTION_ID = re.compile(r"^[a-z][a-z0-9]{0,31}$")
ASCII_SPACES = re.compile(r"[ \t\r\n\f]+")


@dataclass(frozen=True)
class CardPage:
    url: str
    post_id: int
    name: str
    card_type: str  # Type code of the pack: seeker, legacy, terrain, unit, spell, item or relic.
    rank: str | None  # Rank code of the pack, or None (relics).
    factions: tuple[tuple[str, str], ...]  # (id, name), in page order; empty means unaligned.
    traits: tuple[str, ...]
    abilities: tuple[str, ...]
    stats: dict[str, int | None] | None  # None for a null stat (the card's dash)
    image_url: str


class _Node:
    __slots__ = ("tag", "attrs", "children", "parent")

    def __init__(self, tag: str, attrs: dict[str, str], parent: _Node | None) -> None:
        self.tag = tag
        self.attrs = attrs
        self.children: list[_Node | str] = []
        self.parent = parent

    @property
    def classes(self) -> set[str]:
        return set(self.attrs.get("class", "").split())

    def elements(self) -> list[_Node]:
        return [child for child in self.children if isinstance(child, _Node)]

    def descendants(self):  # noqa: ANN201
        for child in self.children:
            if isinstance(child, _Node):
                yield child
                yield from child.descendants()

    def text(self) -> str:
        parts: list[str] = []
        stack: list[_Node | str] = [self]
        while stack:
            item = stack.pop()
            if isinstance(item, str):
                parts.append(item)
            elif item.tag not in ("script", "style"):
                stack.extend(reversed(item.children))
        return ASCII_SPACES.sub(" ", "".join(parts)).strip()


class _TreeBuilder(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.root = _Node("#document", {}, None)
        self._current = self.root

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        node = _Node(tag, {name: value or "" for name, value in attrs}, self._current)
        self._current.children.append(node)
        if tag not in VOID_TAGS:
            self._current = node

    def handle_startendtag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        self._current.children.append(_Node(tag, {name: value or "" for name, value in attrs}, self._current))

    def handle_endtag(self, tag: str) -> None:
        # Closes up to the matching open element; a stray end tag is ignored.
        node: _Node | None = self._current
        while node is not None and node.tag != tag:
            node = node.parent
        if node is not None and node.parent is not None:
            self._current = node.parent

    def handle_data(self, data: str) -> None:
        self._current.children.append(data)


def _only(nodes: list[_Node], what: str) -> _Node:
    if len(nodes) != 1:
        raise PageFormatError(f"expected one {what}, found {len(nodes)}")
    return nodes[0]


def _post_id(root: _Node) -> int:
    links = [node for node in root.descendants() if node.tag == "link" and "shortlink" in node.attrs.get("rel", "").split()]
    href = _only(links, '<link rel="shortlink">').attrs.get("href", "")
    values = parse_qs(urlsplit(href).query).get("p", [])
    if len(values) != 1 or not values[0].isdigit():
        raise PageFormatError(f"the shortlink has no post id: {href!r}")
    return int(values[0])


def _sections(title: _Node) -> dict[str, _Node]:
    """The sections next to the title: a div whose first element is the heading p.ttl-5.ark."""
    container = title.parent
    for _ in range(6):
        if container is None:
            break
        sections: dict[str, _Node] = {}
        for child in container.elements():
            first = next(iter(child.elements()), None)
            if child.tag == "div" and first is not None and first.tag == "p" and {"ttl-5", "ark"} <= first.classes:
                heading = first.text()
                if heading not in SECTIONS:
                    raise PageFormatError(f"unexpected section {heading!r}")
                if heading in sections:
                    raise PageFormatError(f"the section {heading!r} appears twice")
                sections[heading] = child
        if sections:
            return sections
        container = container.parent
    raise PageFormatError("no sections found next to the title")


def _chips(section: _Node, heading: str) -> tuple[str, ...]:
    labels = [node.text() for node in section.descendants() if node.tag == "span" and "comp-chip-label" in node.classes]
    if not labels or any(not label for label in labels):
        raise PageFormatError(f"the section {heading!r} has no labels or an empty one")
    return tuple(labels)


def _factions(section: _Node) -> tuple[tuple[str, str], ...]:
    factions = []
    for link in (node for node in section.descendants() if node.tag == "a"):
        marks = [node for node in link.descendants() if "data-faction-short" in node.attrs]
        faction_id = _only(marks, "data-faction-short in a faction link").attrs["data-faction-short"]
        if not FACTION_URL.match(link.attrs.get("href", "")):
            raise PageFormatError(f"unexpected faction link {link.attrs.get('href')!r}")
        if not FACTION_ID.match(faction_id):
            raise PageFormatError(f"unexpected faction id {faction_id!r}")
        name = link.text()
        if not name:
            raise PageFormatError(f"the faction {faction_id!r} has no name")
        if any(faction_id == known for known, _ in factions):
            raise PageFormatError(f"the faction {faction_id!r} appears twice")
        factions.append((faction_id, name))
    if not factions:
        raise PageFormatError("the section 'Faction' has no faction")
    return tuple(factions)


def _stats(section: _Node) -> dict[str, int | None]:
    stats: dict[str, int | None] = {}
    for css_class, key in STATS.items():
        value = _only([n for n in section.descendants() if css_class in n.classes], f"element .{css_class}").text()
        if value in NULL_STATS:
            stats[key] = None
        elif value.isascii() and value.isdigit():
            stats[key] = int(value)
        else:
            raise PageFormatError(f"the stat {css_class} is neither a number nor a dash: {value!r}")
    return stats


def _type_rank_traits(specifications: tuple[str, ...]) -> tuple[str, str | None, tuple[str, ...]]:
    *rest, rank_word = specifications
    if rank_word not in RANKS:
        raise PageFormatError(f"the last specification is not a rank: {rank_word!r} in {list(specifications)}")
    type_words = [word for word in rest if word in TYPES]
    if len(type_words) != 1:
        raise PageFormatError(f"expected one card type among the specifications {list(specifications)}")
    card_type = TYPES[type_words[0]]
    traits = list(rest)
    traits.remove(type_words[0])
    if "Seeker" in traits:
        if card_type != "unit":
            raise PageFormatError(f"a Seeker that is not a unit: {list(specifications)}")
        card_type = "seeker"
        traits.remove("Seeker")
    if (rank_word == "Relic") != (card_type == "relic"):
        raise PageFormatError(f"only relics have the rank 'Relic': {list(specifications)}")
    if len(set(traits)) != len(traits):
        raise PageFormatError(f"a specification is repeated: {list(specifications)}")
    return card_type, RANKS[rank_word], tuple(traits)


def parse_card_page(html: bytes | str, url: str) -> CardPage:
    """Reads a card page. Raises PageFormatError with the reason if it does not have the expected shape."""
    text = html.decode("utf-8") if isinstance(html, bytes) else html
    builder = _TreeBuilder()
    builder.feed(text)
    builder.close()
    root = builder.root

    title = _only([n for n in root.descendants() if n.tag == "h1" and "compTitle_item" in n.classes], "h1.compTitle_item")
    name = title.text()
    if not name:
        raise PageFormatError("the title is empty")
    image = _only([n for n in root.descendants() if n.tag == "img" and "cardMedia" in n.classes], "img.cardMedia")
    image_url = image.attrs.get("src", "")
    if not IMAGE_URL.match(image_url):
        raise PageFormatError(f"unexpected image URL {image_url!r}")

    sections = _sections(title)
    if "Specifications" not in sections:
        raise PageFormatError("the section 'Specifications' is missing")
    card_type, rank, traits = _type_rank_traits(_chips(sections["Specifications"], "Specifications"))
    factions = _factions(sections["Faction"]) if "Faction" in sections else ()
    abilities = _chips(sections["Skills"], "Skills") if "Skills" in sections else ()
    stats = _stats(sections["Stats"]) if "Stats" in sections else None
    if (stats is not None) != (card_type in UNIT_TYPES):
        raise PageFormatError(f"a card of type {card_type} {'has' if stats else 'lacks'} the section 'Stats'")

    return CardPage(
        url=url,
        post_id=_post_id(root),
        name=name,
        card_type=card_type,
        rank=rank,
        factions=factions,
        traits=traits,
        abilities=abilities,
        stats=stats,
        image_url=image_url,
    )
