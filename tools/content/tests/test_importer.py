# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
"""The whole import against the made-up site, with the real core through mc_cli.

mc_cli comes from $MC_CLI or the release or asan build under $MC_BUILD_ROOT
(tools/ci/ci.sh content sets it).
"""

from __future__ import annotations

import dataclasses
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from mc_content.cache import Cache
from mc_content.core import find_mc_cli
from mc_content.errors import ImportStopped
from mc_content.fetch import Fetcher, Response
from mc_content.importer import Importer

from .fixtures import ASH_DECK, SITE, FakeSite, FakeTime, test_cards

MANUAL = {
    "format": "mc-official-manual",
    "format_version": 1,
    "legacies": {
        "ember-test-heart": {"seeker": "kindra-test-seeker", "starting_echo": 4},
        "dual-test-heart": {"seeker": "dual-test-seeker", "starting_echo": 3},
    },
    "decks": {"ash-test-deck": {"name": "Ash Test Deck", "url": "https://play.example.invalid/decks/ash"}},
}


class ImportTest(unittest.TestCase):
    def setUp(self) -> None:
        self.folder = tempfile.TemporaryDirectory()
        root = Path(self.folder.name)
        self.output = root / "official"
        self.cache_dir = root / "cache"
        self.site = FakeSite(test_cards())
        self.time = FakeTime()
        self.mc_cli = find_mc_cli(None)
        self.output.mkdir()
        (self.output / "seekers-legacies.json").write_text(json.dumps(MANUAL))
        (self.output / "decks").mkdir()
        (self.output / "decks" / "ash-test-deck.txt").write_text(ASH_DECK)

    def tearDown(self) -> None:
        self.folder.cleanup()

    def importer(self, output: Path | None = None, cache: Path | None = None) -> Importer:
        fetcher = Fetcher(self.site, clock=self.time.clock, sleep=self.time.sleep)
        return Importer(fetcher, Cache(cache or self.cache_dir), output or self.output, self.mc_cli,
                        progress=lambda message: None)

    def pack(self) -> dict:
        return json.loads((self.output / "rules-pack.json").read_text(encoding="utf-8"))

    def test_writes_the_pack_images_and_ids(self) -> None:
        outcome = self.importer().run()
        self.assertTrue(outcome.written, outcome.report)
        pack = self.pack()
        self.assertEqual(pack["format_version"], 2)
        self.assertEqual(pack["date"], "2099-01-02")
        self.assertEqual([f["id"] for f in pack["factions"]], ["ash", "tide"])
        cards = {card["id"]: card for card in pack["cards"]}
        self.assertEqual(len(cards), len(test_cards()))
        self.assertEqual(cards["ember-test-heart"]["seeker"], "kindra-test-seeker")
        self.assertEqual(cards["ember-test-heart"]["starting_echo"], 4)
        self.assertEqual(cards["test-plain"]["sigils"], [])
        self.assertEqual(cards["dual-test-unit"]["sigils"], ["ash", "tide"])
        self.assertIsNone(cards["ash-test-unit-1"]["cost"])
        self.assertIsNone(cards["ash-test-unit-1"]["rules_text"])
        self.assertEqual(cards["ash-test-unit-1"]["stats"]["defense"], 9)
        self.assertIsNone(cards["test-totem"]["stats"]["speed"])
        self.assertIsNone(cards["test-totem"]["stats"]["accuracy"])
        self.assertEqual(cards["test-totem"]["stats"]["range"], 0)
        self.assertNotIn("rank", cards["test-scepter"])
        image = cards["test-plain"]["image"]
        self.assertEqual(image["file"], "images/test-plain.png")
        self.assertEqual(hashlib.sha256((self.output / image["file"]).read_bytes()).hexdigest(), image["sha256"])
        self.assertEqual(pack["decks"][0]["id"], "ash-test-deck")
        self.assertEqual(pack["decks"][0]["name"], "Ash Test Deck")
        self.assertEqual(pack["decks"][0]["deck"]["legacy"], "ember-test-heart")
        self.assertTrue((self.output / "card-ids.json").exists())
        self.assertTrue(any("Sigils (RF-06)" in line for line in outcome.report))
        self.assertTrue(any("Card dual-test-unit: ['ash', 'tide']" in line for line in outcome.report))

    def test_the_same_site_gives_the_same_bytes(self) -> None:
        self.assertTrue(self.importer().run().written)
        first = (self.output / "rules-pack.json").read_bytes()
        # Another output and a fresh cache: everything is downloaded again.
        other = Path(self.folder.name) / "other"
        other.mkdir()
        for name in ("seekers-legacies.json",):
            (other / name).write_bytes((self.output / name).read_bytes())
        (other / "decks").mkdir()
        (other / "decks" / "ash-test-deck.txt").write_text(ASH_DECK)
        self.assertTrue(self.importer(other, Path(self.folder.name) / "cache2").run().written)
        self.assertEqual((other / "rules-pack.json").read_bytes(), first)
        self.assertEqual((other / "card-ids.json").read_bytes(), (self.output / "card-ids.json").read_bytes())

    def test_only_changed_pages_and_images_are_downloaded_again(self) -> None:
        self.assertTrue(self.importer().run().written)
        first = (self.output / "rules-pack.json").read_bytes()
        self.site.requests.clear()
        outcome = self.importer().run()
        self.assertEqual(self.site.requests, [SITE + "/robots.txt", SITE + "/card-sitemap.xml"])
        self.assertEqual((self.output / "rules-pack.json").read_bytes(), first)
        self.assertIn("Cards: 0 new, 0 removed, 0 changed", outcome.report)

        # A new lastmod downloads that page again; a new image URL downloads that image.
        cards = test_cards()
        index = next(i for i, c in enumerate(cards) if c.slug == "ash-test-unit-2")
        cards[index] = dataclasses.replace(cards[index], lastmod="2099-02-03T00:00:00+00:00",
                                           image=f"{SITE}/wp-content/uploads/2099/02/TEST-112-v2.png")
        cards[index].stats["statusHealth"] = "8"
        self.site.cards = cards
        self.site.requests.clear()
        outcome = self.importer().run()
        self.assertTrue(outcome.written, outcome.report)
        self.assertEqual(self.site.requests, [SITE + "/robots.txt", SITE + "/card-sitemap.xml", cards[index].url,
                                              cards[index].image_url()])
        report = "\n".join(outcome.report)
        self.assertIn("* ash-test-unit-2", report)
        self.assertIn('stats: {"accuracy": 2', report)
        self.assertEqual(self.pack()["date"], "2099-02-03")

    def test_the_cache_does_not_hide_a_page_whose_lastmod_changed(self) -> None:
        cache = Cache(self.cache_dir)
        card = test_cards()[0]
        cache.store_page(card.url, "2000-01-01", b"old")
        self.assertIsNone(cache.page(card.url, card.lastmod))
        self.assertEqual(cache.page(card.url, "2000-01-01"), b"old")
        (self.cache_dir / cache._pages[card.url]["file"]).write_bytes(b"edited")
        self.assertIsNone(cache.page(card.url, "2000-01-01"))

    def test_a_malformed_page_writes_nothing(self) -> None:
        cards = test_cards()
        cards[3] = dataclasses.replace(cards[3], specs=["Legacy", "Mythic"])
        self.site.cards = cards
        outcome = self.importer().run()
        self.assertFalse(outcome.written)
        self.assertIn(cards[3].url, "\n".join(outcome.report))
        self.assertFalse((self.output / "rules-pack.json").exists())

    def test_a_page_that_cannot_be_downloaded_is_reported(self) -> None:
        url = test_cards()[5].url
        self.site.overrides[url] = [Response(404)]
        outcome = self.importer().run()
        self.assertFalse(outcome.written)
        self.assertIn(f"{url}: {url}: HTTP 404", "\n".join(outcome.report))

    def test_names_import_deck_cannot_tell_apart_stop_the_import(self) -> None:
        cards = test_cards()
        cards.append(dataclasses.replace(cards[-1], slug="copy-test", name="test\u00a0NEUTRAL spell", post_id=999))
        self.site.cards = cards
        with self.assertRaises(ImportStopped) as caught:
            self.importer().run()
        message = str(caught.exception)
        self.assertIn("ambiguous_card", message)
        self.assertIn("'Test Neutral Spell' (test-neutral-spell", message)
        self.assertIn("'test\\xa0NEUTRAL spell' (copy-test", message)  # repr() of a no-break space
        self.assertFalse((self.output / "rules-pack.json").exists())

    def test_an_incomplete_manual_file_stops_the_import(self) -> None:
        (self.output / "seekers-legacies.json").unlink()
        with self.assertRaises(ImportStopped):
            self.importer().run()
        self.assertTrue((self.output / "seekers-legacies.json").exists())
        self.assertFalse((self.output / "rules-pack.json").exists())

    def test_official_decks_that_do_not_pass_are_reported(self) -> None:
        decks = self.output / "decks"
        (decks / "wrong-legacy.txt").write_text(ASH_DECK.replace("Ember Test Heart", "Dual Test Heart"))
        (decks / "too-small.txt").write_text(ASH_DECK.replace("3x Ash Test Spell 4\n", ""))
        (decks / "bad-line.txt").write_text(ASH_DECK.replace("3x Ash Test Unit 1", "3x Not A Test Card"))
        (decks / "Bad Name.txt").write_text(ASH_DECK)
        outcome = self.importer().run()
        self.assertFalse(outcome.written)
        report = "\n".join(outcome.report)
        self.assertIn("wrong-legacy.txt: its Legacy 'Dual Test Heart' goes with 'Dual Test Seeker'", report)
        self.assertIn("too-small.txt: validate_deck: deck_too_small: 27 cards", report)
        self.assertIn("bad-line.txt: line 6: unknown_card", report)
        self.assertIn("Bad Name.txt: the file name", report)
        self.assertNotIn("ash-test-deck.txt", report)
        self.assertFalse((self.output / "rules-pack.json").exists())

    def test_a_missing_sitemap_stops_the_import(self) -> None:
        self.site.overrides[SITE + "/card-sitemap.xml"] = [Response(404)]
        with self.assertRaises(ImportStopped):
            self.importer().run()

    def test_without_ids_the_previous_pack_is_not_overwritten(self) -> None:
        self.assertTrue(self.importer().run().written)
        (self.output / "card-ids.json").unlink()
        with self.assertRaises(ImportStopped):
            self.importer().run()

    def test_stale_images_are_removed(self) -> None:
        self.assertTrue(self.importer().run().written)
        self.site.cards = [c for c in test_cards() if c.slug != "test-scepter"]
        outcome = self.importer().run()
        self.assertTrue(outcome.written, outcome.report)
        self.assertFalse((self.output / "images" / "test-scepter.png").exists())
        self.assertIn("  - test-scepter (Test Scepter)", outcome.report)


if __name__ == "__main__":
    unittest.main()
