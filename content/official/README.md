<!--
SPDX-FileCopyrightText: 2026 Daniel Martín Millet
SPDX-License-Identifier: Apache-2.0
-->

# Official Malediction content (private)

This folder holds, on the development machine only, the pack with the official Malediction
content: card names, texts, stats and images, plus the visual theme with the official fonts,
colors and icons (RF-40 and RNF-22).

Git ignores everything here except this file. **Do not upload it to the repository, to Codeberg,
to continuous integration or to the app stores.** That content belongs to Loot Studios, and its
terms of use forbid reproducing or distributing it without express permission, even on a
non-profit basis. The project is developed as if it had a license (ADR-09), but nothing official
leaves the development machine until there is a written license (ADR-12).

The server's Catalog module serves this pack to the app as signed data. Until there is a license,
only a private development server may serve it (ADR-12 and ADR-13), and importing and signing the
pack stay on the development machine.

## What goes here

The importer writes the pack, its images and the card ids; the manual file and the official
decks are written by hand; the theme arrives in phase 1:

```text
content/official/
├── rules-pack.json         Pack with the official cards (mc-rules-pack format 2), from the importer
├── rules-pack.json.sig     Detached Ed25519 signature of the pack (64 bytes, ADR-06), from mc_pack_tool sign
├── images/                 Card images, images/<card id>.<ext>; the pack holds the SHA-256 of each one
├── card-ids.json           Stable id of each card, tied to the post id of its page; do not lose it
├── seekers-legacies.json   By hand: the Seeker and the starting echo of each Legacy, the name and link of each official deck
├── decks/                  By hand: official decks exported from the Deck Builder, one <deck id>.txt each
└── theme/
    ├── theme.json          Colors, fonts and icons of the official theme
    ├── fonts/              Official fonts, licensed for embedding in an app
    └── icons/              Official icons: sigils, echo, mastery
```

`rules-pack.json` uses the same format as the made-up pack in
[`content/test/rules-pack.test.json`](../test/rules-pack.test.json). Format 2 adds the factions,
the traits, abilities and stats of each card (each stat a number or null: the card's dash, shown as
–), its image with its hash and source URL, a `cost` and
a `rules_text` that are null until the pack gives them, and the official decks (RF-61).

## How to update it

```sh
tools/content/import_official.py
mc_pack_tool sign ~/.config/seekers-companion/keys/dev.mcsk content/official/rules-pack.json
```

The importer ([`tools/content/`](../../tools/content/)) reads only the public pages of the card
library (robots.txt, `card-sitemap.xml`, each `/card/<slug>/` page and its image), one request at
a time with at least 2 seconds between them. It keeps what it downloads in
`~/.cache/seekers-companion/content/` and only downloads a page again when its `lastmod` in the
sitemap changes, and an image when its URL changes. It writes nothing here until every step
passes, and it stops and says why when:

- the site answers 401 or 403, or 429 or 5xx twice;
- a page does not have the expected shape;
- two cards have names that `import_deck` cannot tell apart (they would be `ambiguous_card`, and
  `export_deck` could not write them);
- `seekers-legacies.json` is missing or incomplete (the first time, it writes it with every
  Seeker and Legacy and empty values, to be filled in);
- an official deck in `decks/` does not read with `import_deck`, does not pass `validate_deck` on
  the new pack, or its Legacy is not the one `seekers-legacies.json` gives its Seeker.

Then it prints the new, removed and changed cards against the previous pack. A changed pack must
be signed again. The same pages give the same bytes.

**Card ids.** A card's id is the slug of its page the first time (for example
`ash-test-unit`), and `card-ids.json` ties it to the WordPress post id of the page. If the slug
changes, the id stays; an id is never reused. If a new page has the slug or the name of a known
card, the importer stops so that you decide whether it is the same card. Decks saved on the server
are checked against every published version of the pack (ADR-13), so ids must not change.

**Factions.** The ids are the site's short names (`order`, `conclave`, `legion`, `primal`) and the
names come from the pages. A card without a faction has no sigils: it is unaligned.

## Where it comes from

- **Cards and images:** from the public card library of malediction.gg, for development only.
- **Seekers, Legacies and starting echo; official decks:** by hand, because the pages do not
  give them. The official decks are exported from the Deck Builder (Malediction Team Official).
- **Fonts, colors and icons:** there is no public brand guide. Loot Studios offers a brand kit
  to stores, and it has to be requested (suporte@malediction.gg). Each font also needs a license
  that allows embedding it in an app.
- **Accessibility:** the official colors are used where they meet the contrast required by
  RNF-13; otherwise, only as an accent.

Without the official pack there are no cards, because the app only uses the official cards that
the server serves (ADR-13). The neutral theme only stands in for the official fonts, colors and
icons when they cannot be used.
