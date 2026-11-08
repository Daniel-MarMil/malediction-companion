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

## What goes here

Planned layout. The core already reads the rules pack; the theme arrives in phase 1:

```text
content/official/
├── rules-pack.json        Rules pack with the official cards (mc-rules-pack format)
├── rules-pack.json.sig    Detached Ed25519 signature of the pack (64 bytes, ADR-06)
├── images/                Card images, one per card id
└── theme/
    ├── theme.json         Colors, fonts and icons of the official theme
    ├── fonts/             Official fonts, licensed for embedding in an app
    └── icons/             Official icons: sigils, echo, mastery
```

`rules-pack.json` uses the same format as the made-up pack in
[`content/test/rules-pack.test.json`](../test/rules-pack.test.json).

## Where it comes from

- **Cards and images:** from the official card library, for development only.
- **Fonts, colors and icons:** there is no public brand guide. Loot Studios offers a brand kit
  to stores, and it has to be requested (suporte@malediction.gg). Each font also needs a license
  that allows embedding it in an app.
- **Accessibility:** the official colors are used where they meet the contrast required by
  RNF-13; otherwise, only as an accent.

Without this folder, the app uses a neutral theme and the app's own card sheets (RF-03).
