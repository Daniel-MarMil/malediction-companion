# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
"""Made-up card pages and sitemaps with the same shape as malediction.gg, for tests without a
network. None of these cards, names or images are Malediction content."""

from __future__ import annotations

import html
from dataclasses import dataclass, field

from mc_content.fetch import Response

SITE = "https://malediction.gg"
LASTMOD = "2099-01-02T03:04:05+00:00"
PNG = b"\x89PNG\r\n\x1a\n" + b"made-up test image "


@dataclass
class FakeCard:
    slug: str
    name: str
    post_id: int
    specs: list[str]
    factions: list[tuple[str, str]] = field(default_factory=list)  # (short id, name)
    skills: list[str] = field(default_factory=list)
    stats: dict[str, str] | None = None
    image: str = ""
    lastmod: str = LASTMOD

    @property
    def url(self) -> str:
        return f"{SITE}/card/{self.slug}/"

    def image_url(self) -> str:
        return self.image or f"{SITE}/wp-content/uploads/2099/01/TEST-{self.post_id}.png"


FACTION_NAMES = {"ash": "Ash Test Faction", "tide": "Tide Test Faction"}
UNIT_STATS = {
    "statusAttack": "2",
    "statusPowerHit": "3",
    "statusPowerGraze": "1",
    "statusRange": "0",
    "statusMovement": "4",
    "statusDefense": "9",
    "statusHealth": "7",
}


def _chips(labels: list[str]) -> str:
    return "".join(
        '<div id="_" class="comp-chip color-system-v2 cont row comp-chip-type-static comp-chip-style-infox">'
        '<div class="comp-chip-content cont row nwrap v-cent round hover fullh">'
        f'<span class="comp-chip-label tx-sml xs-letter-spc">{html.escape(label)}</span>'
        "</div></div>"
        for label in labels
    )


def _section(heading: str, body: str) -> str:
    return (
        f'<div class="cont col fullw g-2"><p class="ttl-5 ark d-on-surface-cl"> {heading} </p>'
        f'<div class="cont row fullw g-2">{body}</div></div>'
    )


def _faction_link(short: str, name: str) -> str:
    return (
        f'<a href="{SITE}/faction/{short}-test/" class="cont row g-2 v-cent">'
        f'<div class="cont col h-cent v-cent librarySingleFactionIcon" data-faction-short="{short}"><svg></svg></div>'
        f'<p class="tx-nav d-on-surface-cl"> {html.escape(name)} </p></a>'
    )


def card_html(card: FakeCard, *, extra_section: str = "", title_class: str = "compTitle_item") -> str:
    sections = []
    if card.factions:
        links = "".join(_faction_link(short, name) for short, name in card.factions)
        sections.append(_section("Faction", f'<div class="cont row g-4 v-cent nwrap">{links}</div>'))
    if card.stats is not None:
        stats = "".join(
            '<div class="cont row co-01 m-co-01 v-cent h-cent"><div class="cont col v-cent h-cent statusIcon">'
            '<div class="compTooltip"><p class="tx-tny d-on-surface-cl">Tip</p></div></div>'
            f'<div class="cont col v-cent h-cent statusIcon"><h5 class="ttl-5 ark fcent {css}"> {value} </h5></div></div>'
            for css, value in card.stats.items()
        )
        sections.append(_section("Stats", f'<div id="ResourceStatus" class="cont row fullw">{stats}</div>'))
    if card.skills:
        sections.append(_section("Skills", _chips(card.skills)))
    if card.specs:
        sections.append(_section("Specifications", _chips(card.specs)))
    if extra_section:
        sections.append(extra_section)
    # A faction link in the menu, outside the sections, as on the real pages.
    menu = _faction_link("ash", "Ash Test Faction")
    return f"""<!DOCTYPE html>
<html lang="en-US"><head>
<meta charset="UTF-8">
<title>{html.escape(card.name)} - Test</title>
<link rel="canonical" href="{card.url}" />
<link rel='shortlink' href='{SITE}/?p={card.post_id}' />
<script>var x = "<h1 class='compTitle_item'>not this</h1>";</script>
</head><body>
<header><nav>{menu}</nav></header>
<main><section><div class="cont row">
<div class="cont col co-06"><div class="compInteractiveCard cardContainer"><div class="cardMediaContainer">
<img class="cardMedia cont col fullw round" src="{card.image_url()}" loading="lazy">
</div></div></div>
<div class="cont col co-06 m-co-04 pv-4 g-4">
<div class="cont col fullw pv-3 dsk"><div class="cont col h-start g-1 compTitle">
<h1 class="{title_class} ttl-1 ark"> {html.escape(card.name)} </h1><svg width="115"></svg>
</div></div>
{''.join(sections)}
</div></div></section></main>
<footer><a class="ttl-5 ark d-secondary-cl" href="#"> Game </a></footer>
</body></html>
"""


def sitemap_xml(cards: list[FakeCard]) -> str:
    urls = "".join(f"<url><loc>{card.url}</loc><lastmod>{card.lastmod}</lastmod></url>" for card in cards)
    return (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">' + urls + "</urlset>"
    )


def unit(slug: str, name: str, post_id: int, rank: str = "Basic", factions: tuple[str, ...] = ("ash",)) -> FakeCard:
    return FakeCard(
        slug,
        name,
        post_id,
        ["Test Kin", "Unit", rank],
        [(short, FACTION_NAMES[short]) for short in factions],
        ["Test Strike"],
        dict(UNIT_STATS),
    )


def spell(slug: str, name: str, post_id: int, factions: tuple[str, ...] = ("ash",)) -> FakeCard:
    return FakeCard(slug, name, post_id, ["Spell", "Channel", "Basic"], [(s, FACTION_NAMES[s]) for s in factions])


def test_cards() -> list[FakeCard]:
    """A small made-up library: two Seekers (one dual), their Legacies, a terrain, a relic,
    an item, and enough ash cards for a 30-card deck."""
    cards = [
        FakeCard("kindra-test-seeker", "Kindra, the Test Seeker", 101, ["Test Kin", "Seeker", "Unit", "Legendary"],
                 [("ash", FACTION_NAMES["ash"])], ["Test Aura"], dict(UNIT_STATS)),
        FakeCard("dual-test-seeker", "Dual Test Seeker", 102, ["Seeker", "Unit", "Legendary"],
                 [("ash", FACTION_NAMES["ash"]), ("tide", FACTION_NAMES["tide"])], [], dict(UNIT_STATS)),
        FakeCard("ember-test-heart", "Ember Test Heart", 103, ["Legacy", "Legendary"], [("ash", FACTION_NAMES["ash"])]),
        FakeCard("dual-test-heart", "Dual Test Heart", 104, ["Legacy", "Legendary"],
                 [("ash", FACTION_NAMES["ash"]), ("tide", FACTION_NAMES["tide"])]),
        FakeCard("test-plain", "Test Plain", 105, ["Terrain", "Legendary"]),
        FakeCard("test-scepter", "Test Scepter", 106, ["Attachment", "Relic", "Relic"], [], ["Range +"]),
        FakeCard("test-gloves", "Test Gloves", 107, ["Attachment", "Item", "Elite"], [], ["Accuracy +"]),
        unit("tide-test-unit", "Tide Test Unit", 108, factions=("tide",)),
        unit("dual-test-unit", "Dual Test Unit", 109, rank="Elite", factions=("ash", "tide")),
    ]
    for number in range(1, 7):
        cards.append(unit(f"ash-test-unit-{number}", f"Ash Test Unit {number}", 110 + number))
    for number in range(1, 5):
        cards.append(spell(f"ash-test-spell-{number}", f"Ash Test Spell {number}", 120 + number))
    cards.append(spell("test-neutral-spell", "Test Neutral Spell", 130, factions=()))
    totem = unit("test-totem", "Test Totem", 131)
    totem.stats.update(statusMovement="--", statusAttack="-")  # Null stats, as the site writes them.
    cards.append(totem)
    return cards


ASH_DECK = """1x Kindra, the Test Seeker
1x Ember Test Heart
1x Test Plain

Main Deck
3x Ash Test Unit 1
3x Ash Test Unit 2
3x Ash Test Unit 3
3x Ash Test Unit 4
3x Ash Test Unit 5
3x Ash Test Unit 6
3x Ash Test Spell 1
3x Ash Test Spell 2
3x Ash Test Spell 3
3x Ash Test Spell 4

Side Deck
2x Test Gloves
"""

ROBOTS = "User-agent: *\nDisallow:\n"


class FakeSite:
    """A transport that serves the made-up library and records every request."""

    def __init__(self, cards: list[FakeCard], robots: str = ROBOTS) -> None:
        self.cards = cards
        self.robots = robots
        self.requests: list[str] = []
        self.overrides: dict[str, list[Response]] = {}  # URL -> replies to give first

    def page_body(self, card: FakeCard) -> bytes:
        return card_html(card).encode("utf-8")

    def get(self, url: str, headers: dict[str, str], max_bytes: int) -> Response:
        self.requests.append(url)
        assert headers["User-Agent"].startswith("seekers-companion-content-importer/")
        if self.overrides.get(url):
            return self.overrides[url].pop(0)
        if url == SITE + "/robots.txt":
            return Response(200, self.robots.encode())
        if url == SITE + "/card-sitemap.xml":
            return Response(200, sitemap_xml(self.cards).encode())
        for card in self.cards:
            if url == card.url:
                return Response(200, self.page_body(card))
            if url == card.image_url():
                return Response(200, PNG + card.image_url().encode())
        return Response(404)


class FakeTime:
    """A clock that only moves when someone sleeps, so tests do not wait."""

    def __init__(self) -> None:
        self.now = 1000.0
        self.sleeps: list[float] = []

    def clock(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.sleeps.append(seconds)
        self.now += seconds
