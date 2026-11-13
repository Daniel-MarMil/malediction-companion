# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
"""Reads card-sitemap.xml: the public index of the card pages, with their lastmod."""

from __future__ import annotations

import re
import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass

from .errors import ImportStopped

NAMESPACE = "{http://www.sitemaps.org/schemas/sitemap/0.9}"
CARD_URL = re.compile(r"^https://malediction\.gg/card/([a-z0-9]+(?:-[a-z0-9]+)*)/$")
MAX_ENTRIES = 5000


@dataclass(frozen=True)
class SitemapEntry:
    url: str
    slug: str
    lastmod: str | None


def parse_sitemap(data: bytes) -> list[SitemapEntry]:
    """The card pages of the sitemap, sorted by URL. Stops if the sitemap is not as expected."""
    if b"<!DOCTYPE" in data or b"<!ENTITY" in data:
        raise ImportStopped("the sitemap declares a DOCTYPE or entities; it is not read")
    try:
        root = ElementTree.fromstring(data)
    except ElementTree.ParseError as error:
        raise ImportStopped(f"the sitemap is not valid XML: {error}") from error
    if root.tag != NAMESPACE + "urlset":
        raise ImportStopped(f"the sitemap is not a urlset: {root.tag}")
    entries: dict[str, SitemapEntry] = {}
    for item in root.findall(NAMESPACE + "url"):
        location = (item.findtext(NAMESPACE + "loc") or "").strip()
        match = CARD_URL.match(location)
        if match is None:
            raise ImportStopped(f"the sitemap has a URL that is not a card page: {location!r}")
        lastmod = (item.findtext(NAMESPACE + "lastmod") or "").strip() or None
        if location in entries:
            raise ImportStopped(f"the sitemap lists {location} twice")
        entries[location] = SitemapEntry(location, match.group(1), lastmod)
    if not entries:
        raise ImportStopped("the sitemap lists no card pages")
    if len(entries) > MAX_ENTRIES:
        raise ImportStopped(f"the sitemap lists more than {MAX_ENTRIES} pages")
    return [entries[url] for url in sorted(entries)]
