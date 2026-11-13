# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
"""Reading card pages, and failing clearly when a page does not have the expected shape."""

from __future__ import annotations

import dataclasses
import unittest

from mc_content.card_page import parse_card_page
from mc_content.errors import PageFormatError

from .fixtures import UNIT_STATS, FakeCard, card_html, test_cards


def card(slug: str) -> FakeCard:
    return next(c for c in test_cards() if c.slug == slug)


def parse(fake: FakeCard, **options: str):
    return parse_card_page(card_html(fake, **options).encode("utf-8"), fake.url)


class ReadsCards(unittest.TestCase):
    def test_a_seeker(self) -> None:
        page = parse(card("kindra-test-seeker"))
        self.assertEqual(page.name, "Kindra, the Test Seeker")
        self.assertEqual(page.post_id, 101)
        self.assertEqual(page.card_type, "seeker")
        self.assertEqual(page.rank, "legendary")
        self.assertEqual(page.factions, (("ash", "Ash Test Faction"),))
        self.assertEqual(page.traits, ("Test Kin",))
        self.assertEqual(page.abilities, ("Test Aura",))
        self.assertEqual(
            page.stats,
            {"accuracy": 2, "power_hit": 3, "power_graze": 1, "range": 0, "speed": 4, "defense": 9, "max_health": 7},
        )
        self.assertTrue(page.image_url.endswith("/TEST-101.png"))

    def test_a_dual_seeker_keeps_both_factions(self) -> None:
        page = parse(card("dual-test-seeker"))
        self.assertEqual([faction for faction, _ in page.factions], ["ash", "tide"])

    def test_other_types(self) -> None:
        expected = {
            "ember-test-heart": ("legacy", "legendary", ()),
            "test-plain": ("terrain", "legendary", ()),
            "test-scepter": ("relic", None, ("Attachment",)),
            "test-gloves": ("item", "elite", ("Attachment",)),
            "ash-test-spell-1": ("spell", "basic", ("Channel",)),
        }
        for slug, (card_type, rank, traits) in expected.items():
            page = parse(card(slug))
            self.assertEqual((page.card_type, page.rank, page.traits), (card_type, rank, traits), slug)
            self.assertIsNone(page.stats)

    def test_an_unaligned_card_has_no_factions_even_with_faction_links_in_the_menu(self) -> None:
        self.assertEqual(parse(card("test-plain")).factions, ())

    def test_a_dash_is_a_null_stat_in_any_of_its_three_spellings(self) -> None:
        for dash in ("-", "--", "\u2013"):
            stats = dict(UNIT_STATS, statusRange=dash, statusMovement=dash)
            page = parse(dataclasses.replace(card("ash-test-unit-1"), stats=stats))
            self.assertIsNone(page.stats["range"], dash)
            self.assertIsNone(page.stats["speed"], dash)
            self.assertEqual(page.stats["accuracy"], 2)

    def test_names_keep_typographic_apostrophes_and_entities(self) -> None:
        fake = dataclasses.replace(card("ash-test-spell-1"), name="Kindra’s Test & Spell")
        self.assertEqual(parse(fake).name, "Kindra’s Test & Spell")


class FailsOnUnexpectedShapes(unittest.TestCase):
    def assert_fails(self, fake: FakeCard, fragment: str, **options: str) -> None:
        with self.assertRaises(PageFormatError) as caught:
            parse(fake, **options)
        self.assertIn(fragment, str(caught.exception))

    def test_without_a_title(self) -> None:
        self.assert_fails(card("test-plain"), "h1.compTitle_item", title_class="other")

    def test_an_unknown_section(self) -> None:
        extra = '<div class="cont col"><p class="ttl-5 ark"> Cost </p><div>3</div></div>'
        self.assert_fails(card("test-plain"), "unexpected section 'Cost'", extra_section=extra)

    def test_without_specifications(self) -> None:
        self.assert_fails(dataclasses.replace(card("ash-test-unit-1"), specs=[]), "'Specifications' is missing")
        self.assert_fails(dataclasses.replace(card("test-plain"), specs=[]), "no sections")

    def test_an_unknown_rank_or_type(self) -> None:
        self.assert_fails(dataclasses.replace(card("test-plain"), specs=["Terrain", "Mythic"]), "not a rank")
        self.assert_fails(dataclasses.replace(card("test-plain"), specs=["Landmark", "Basic"]), "one card type")
        self.assert_fails(dataclasses.replace(card("test-plain"), specs=["Unit", "Spell", "Basic"]), "one card type")

    def test_relic_rank_only_on_relics(self) -> None:
        self.assert_fails(dataclasses.replace(card("test-gloves"), specs=["Item", "Relic"]), "only relics")
        self.assert_fails(dataclasses.replace(card("test-scepter"), specs=["Relic", "Basic"]), "only relics")

    def test_a_seeker_must_be_a_unit(self) -> None:
        self.assert_fails(dataclasses.replace(card("ember-test-heart"), specs=["Seeker", "Legacy", "Legendary"]),
                          "not a unit")

    def test_stats_only_on_units(self) -> None:
        self.assert_fails(dataclasses.replace(card("ash-test-unit-1"), stats=None), "lacks the section 'Stats'")
        self.assert_fails(dataclasses.replace(card("test-plain"), stats=dict(UNIT_STATS)), "has the section 'Stats'")

    def test_a_stat_that_is_not_a_number_or_is_missing(self) -> None:
        for text in ("X", "---", "\u2014", "-1", "", "3+"):
            stats = dict(UNIT_STATS, statusHealth=text)
            self.assert_fails(dataclasses.replace(card("ash-test-unit-1"), stats=stats), "statusHealth")
        stats = dict(UNIT_STATS)
        del stats["statusRange"]
        self.assert_fails(dataclasses.replace(card("ash-test-unit-1"), stats=stats), "statusRange")

    def test_a_bad_image_url(self) -> None:
        self.assert_fails(dataclasses.replace(card("test-plain"), image="https://example.invalid/x.png"), "image URL")

    def test_without_a_post_id(self) -> None:
        text = card_html(card("test-plain")).replace("?p=105", "?page=105")
        with self.assertRaises(PageFormatError):
            parse_card_page(text, card("test-plain").url)

    def test_two_images(self) -> None:
        text = card_html(card("test-plain")).replace("</main>", '<img class="cardMedia" src="x"></main>')
        with self.assertRaises(PageFormatError):
            parse_card_page(text, card("test-plain").url)


if __name__ == "__main__":
    unittest.main()
