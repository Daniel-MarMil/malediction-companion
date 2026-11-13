# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
"""The sitemap, the stable card ids and the manual Seeker-Legacy file."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from mc_content.card_page import parse_card_page
from mc_content.errors import ImportStopped
from mc_content.ids import IdRegistry
from mc_content.manual import load_manual
from mc_content.sitemap import parse_sitemap

from .fixtures import FakeCard, card_html, sitemap_xml, test_cards


class Sitemap(unittest.TestCase):
    def test_reads_urls_and_lastmod_sorted(self) -> None:
        entries = parse_sitemap(sitemap_xml(list(reversed(test_cards()))).encode())
        self.assertEqual([e.url for e in entries], sorted(e.url for e in entries))
        self.assertEqual(entries[0].lastmod, "2099-01-02T03:04:05+00:00")

    def test_stops_on_anything_unexpected(self) -> None:
        good = sitemap_xml(test_cards()[:1])
        for bad in (
            "not xml",
            good.replace("urlset", "sitemapindex"),
            good.replace("/card/", "/news/"),
            good.replace("</urlset>", good[good.index("<url>") : good.index("</urlset>")] + "</urlset>"),
            '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9"></urlset>',
            '<!DOCTYPE x [<!ENTITY a "b">]>' + good,
        ):
            with self.assertRaises(ImportStopped, msg=bad[:60]):
                parse_sitemap(bad.encode())


def pages(cards: list[FakeCard]):
    return [parse_card_page(card_html(card), card.url) for card in cards]


class Ids(unittest.TestCase):
    def test_new_ids_come_from_the_slug_and_survive_a_reload(self) -> None:
        registry = IdRegistry()
        ids, notes = registry.assign(pages(test_cards()))
        self.assertEqual(ids["https://malediction.gg/card/test-plain/"], "test-plain")
        self.assertEqual(notes, [])
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "card-ids.json"
            path.write_bytes(registry.to_bytes())
            again, _ = IdRegistry.load(path).assign(pages(list(reversed(test_cards()))))
        self.assertEqual(again, ids)

    def test_a_new_slug_keeps_the_id(self) -> None:
        registry = IdRegistry()
        registry.assign(pages(test_cards()))
        moved = FakeCard("plain-of-tests", "Test Plain", 105, ["Terrain", "Legendary"])
        ids, notes = registry.assign(pages([moved]))
        self.assertEqual(ids[moved.url], "test-plain")
        self.assertIn("moved from /card/test-plain/ to /card/plain-of-tests/", notes[0])

    def test_a_new_post_with_a_known_slug_or_name_stops(self) -> None:
        registry = IdRegistry()
        registry.assign(pages(test_cards()))
        for card in (
            FakeCard("test-plain", "Another Name", 999, ["Terrain", "Legendary"]),
            FakeCard("new-slug", "Test Plain", 998, ["Terrain", "Legendary"]),
        ):
            with self.assertRaises(ImportStopped):
                registry.assign(pages([card]))

    def test_a_retired_id_is_never_reused(self) -> None:
        registry = IdRegistry()
        registry.assign(pages(test_cards()))
        registry.entries[[e.id for e in registry.entries].index("test-plain")].slug = "old-slug"
        registry.entries[[e.id for e in registry.entries].index("test-plain")].name = "Old Name"
        ids, _ = registry.assign(pages([FakeCard("test-plain", "Brand New", 500, ["Terrain", "Legendary"])]))
        self.assertEqual(ids["https://malediction.gg/card/test-plain/"], "test-plain-2")

    def test_two_pages_with_the_same_post_stop(self) -> None:
        cards = [FakeCard("a-test", "A", 7, ["Terrain", "Legendary"]), FakeCard("b-test", "B", 7, ["Terrain", "Legendary"])]
        with self.assertRaises(ImportStopped):
            IdRegistry().assign(pages(cards))


SEEKERS = {"kindra-test-seeker": "Kindra, the Test Seeker", "dual-test-seeker": "Dual Test Seeker"}
LEGACIES = {"ember-test-heart": "Ember Test Heart", "dual-test-heart": "Dual Test Heart"}


class ManualFile(unittest.TestCase):
    def setUp(self) -> None:
        self.folder = tempfile.TemporaryDirectory()
        self.path = Path(self.folder.name) / "seekers-legacies.json"

    def tearDown(self) -> None:
        self.folder.cleanup()

    def write(self, legacies: dict) -> None:
        self.path.write_text(json.dumps({"format": "mc-official-manual", "format_version": 1, "legacies": legacies}))

    def test_writes_a_template_and_stops_when_missing(self) -> None:
        with self.assertRaises(ImportStopped):
            load_manual(self.path, SEEKERS, LEGACIES)
        template = json.loads(self.path.read_text())
        self.assertEqual(template["legacies"]["ember-test-heart"], {"name": "Ember Test Heart", "seeker": None,
                                                                     "starting_echo": None})
        self.assertEqual(template["seekers"], dict(sorted(SEEKERS.items())))
        with self.assertRaises(ImportStopped) as caught:  # The template is still empty.
            load_manual(self.path, SEEKERS, LEGACIES)
        self.assertIn("starting_echo", str(caught.exception))

    def test_reads_a_complete_file(self) -> None:
        self.write({"ember-test-heart": {"seeker": "kindra-test-seeker", "starting_echo": 4},
                    "dual-test-heart": {"seeker": "dual-test-seeker", "starting_echo": 0}})
        manual = load_manual(self.path, SEEKERS, LEGACIES)
        self.assertEqual(manual.legacies["ember-test-heart"].starting_echo, 4)

    def test_stops_on_missing_or_wrong_data(self) -> None:
        cases = [
            {"ember-test-heart": {"seeker": "kindra-test-seeker", "starting_echo": None},
             "dual-test-heart": {"seeker": "dual-test-seeker", "starting_echo": 3}},
            {"ember-test-heart": {"seeker": "nobody", "starting_echo": 4},
             "dual-test-heart": {"seeker": "dual-test-seeker", "starting_echo": 3}},
            {"ember-test-heart": {"seeker": "kindra-test-seeker", "starting_echo": True},
             "dual-test-heart": {"seeker": "dual-test-seeker", "starting_echo": 3}},
            {"ember-test-heart": {"seeker": "kindra-test-seeker", "starting_echo": 4},
             "dual-test-heart": {"seeker": "kindra-test-seeker", "starting_echo": 3}},  # a Seeker without Legacy
            {"ember-test-heart": {"seeker": "kindra-test-seeker", "starting_echo": 4},
             "dual-test-heart": {"seeker": "dual-test-seeker", "starting_echo": 3},
             "ghost-heart": {"seeker": "dual-test-seeker", "starting_echo": 3}},
        ]
        for legacies in cases:
            self.write(legacies)
            with self.assertRaises(ImportStopped, msg=json.dumps(legacies)):
                load_manual(self.path, SEEKERS, LEGACIES)

    def test_adds_new_legacies_with_empty_values_and_stops(self) -> None:
        self.write({"ember-test-heart": {"seeker": "kindra-test-seeker", "starting_echo": 4}})
        with self.assertRaises(ImportStopped) as caught:
            load_manual(self.path, SEEKERS, LEGACIES)
        self.assertIn("dual-test-heart", str(caught.exception))
        data = json.loads(self.path.read_text())
        self.assertEqual(data["legacies"]["ember-test-heart"]["starting_echo"], 4)
        self.assertIsNone(data["legacies"]["dual-test-heart"]["seeker"])


if __name__ == "__main__":
    unittest.main()
