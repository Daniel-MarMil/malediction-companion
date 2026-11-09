<!--
SPDX-FileCopyrightText: 2026 Daniel Martín Millet
SPDX-License-Identifier: Apache-2.0
-->

# Seekers Companion: notes for Claude Code

Unofficial companion app for Malediction, the card game by Loot Studios. The repository holds
the shared C++20 core (`core/`) with its C API; the Android app (Kotlin, Jetpack Compose) and
the iOS app (Swift, SwiftUI) come later. Read `docs/code-tour.md` first; the decisions are in
`docs/adr/`.

## Language

- Everything in the repository is in English: code, comments, documentation, scripts, test data
  and commit messages.
- The only Spanish text is the architecture and requirements document, which lives outside the
  repository. Requirement IDs (RF-xx, RNF-xx) refer to it.
- If a change affects a requirement or an ADR, stop and tell Daniel, so that he can record the
  decision in that document.

## Build and test

Run everything from the repository root in WSL. Builds go to `$MC_BUILD_ROOT`
(`~/.cache/seekers-companion/build`), never inside the repository.

- `tools/ci/ci.sh asan`: builds the core and runs every test with AddressSanitizer and UBSan.
  Run it after every change.
- `tools/ci/ci.sh lint`: clang-format and clang-tidy. Run it before proposing a commit.
- `tools/ci/ci.sh coverage`: fails below 90 % line coverage of `core/src`.
- `tools/ci/ci.sh all`: everything continuous integration runs, including ThreadSanitizer,
  Release, fuzzing and the Android library.
- A single test: `ctest --test-dir "$MC_BUILD_ROOT/asan" -R <name> --output-on-failure`.
- The C API by hand:
  `"$MC_BUILD_ROOT/asan/examples/mc_cli" content/test/rules-pack.test.json examples/commands/invalid-deck.json`.

## Code rules

- C++20 with warnings as errors; the code must pass `.clang-tidy` and `.clang-format`.
- The apps reach the core only through `core/include/mc/mc_api.h` (ADR-11): JSON commands and
  replies as bytes, no exception crosses the boundary, and every returned buffer is freed with
  `mc_buffer_free`. The shared library exports only `mc_*` (`core/mc_core.map`).
- Rules and limits come from the rules pack, never from the code (ADR-06).
- Every rule change comes with tests: GoogleTest cases and, where it fits, a RapidCheck property
  in `tests/deck_properties_test.cpp`.
- Every file starts with the Apache-2.0 SPDX header (`SPDX-FileCopyrightText: 2026 Daniel Martín
  Millet`); files that cannot hold comments, such as JSON, are covered in `REUSE.toml`.
  `tools/ci/check_licenses.py` checks it.
- Dependencies only under MIT, BSD, Apache, ISC, BSL, zlib or public domain licenses; nothing
  GPL, LGPL or AGPL in what ships (ADR-08).
- Keep `README.md`, `docs/code-tour.md` and the ADRs in step with the code.

## Official content

Never put official Malediction content (card names, texts, images, fonts, colors or icons) in
the repository, in commits, in continuous integration or in the app stores (ADR-09 and ADR-12).
It lives only in `content/official/`, which git ignores. Tests use the made-up cards in
`content/test/rules-pack.test.json`; real decklists, such as those exported from malediction.gg,
stay out of the repository too.

## Git and security

- Commit only when Daniel asks, as `Daniel Martín Millet <Daniel-MarMil@noreply.codeberg.org>`
  (already in the repository's git config). Daniel pushes: never push, force-push or rewrite
  published history.
- Commit messages: a short imperative subject and a body that explains why.
- `origin` fetches from Codeberg and pushes to Codeberg and GitHub.
- Never write passwords or tokens into files, commands or URLs; Daniel signs in himself.

## Law

The app must comply with the GDPR and the rest of EU law, and all its code is free software under
the Apache License 2.0.
