<!--
SPDX-FileCopyrightText: 2026 Daniel Martín Millet
SPDX-License-Identifier: Apache-2.0
-->

# Seekers Companion

A digital companion for games of [Malediction](https://malediction.gg), the card game with
miniatures by Loot Studios: decks that follow the rules, echo and mastery counters kept in sync
between phones, and a match log. It takes the flow of the Warhammer 40,000 app as a reference,
adapted to a card game.

**Unofficial project.** It is not affiliated with or endorsed by Loot Studios.

## Status

Phase 0: the shared C++20 core.

- Versioned rules packs (`mc-rules-pack` format 1) with a detached Ed25519 signature (ADR-06).
- Deck validation (RF-06): Seeker, Legacy and terrain; 30 to 50 cards; sigils; copies per rank;
  a side deck of 7. The limits come from the pack, not from the code.
- C API for the apps (ADR-11).
- Tests with GoogleTest and RapidCheck, sanitizers, coverage and fuzzing with libFuzzer.

The Android app (Kotlin with Jetpack Compose) comes next, and then the iOS app (Swift with
SwiftUI). The iOS app needs a Mac with Xcode; until there is one, work continues on Android.

Requirement IDs (RF-xx for functional and RNF-xx for non-functional requirements) and the phases
refer to the project's architecture and requirements document, which is written in Spanish.

## Official content

The project is developed with the official Malediction cards, images, fonts, colors and icons
as if it had a license (ADR-09), but that content **is not in the repository**: it lives in
`content/official/`, which git ignores (ADR-12). The malediction.gg terms forbid reproducing or
distributing it without express permission, even on a non-profit basis, so a license must be
requested from Loot Studios before publishing any version. More details in
[`content/official/README.md`](content/official/README.md).

The tests use a pack of made-up cards: [`content/test/rules-pack.test.json`](content/test/rules-pack.test.json).

## Layout

```text
core/                 C++20 core (no user interface, no I/O)
  include/mc/mc_api.h   C API, the only boundary with the apps
  include/mc/core/      Domain types: cards, rules pack, deck, validation
  src/                  Implementation
tests/                Tests (GoogleTest, RapidCheck) and the C11 header check
fuzz/                 libFuzzer targets and seed corpus
content/test/         Made-up rules pack for the tests
content/official/     Private official content (ignored by git)
docs/                 Architecture decisions (adr/) and local environment guide
tools/ci/             Continuous integration tasks, also for local use
tools/dev/            Local environment setup
.woodpecker/          Woodpecker CI workflows
```

## Building

To set up the full environment on Windows (WSL 2, Android Studio and, optionally, MSVC), follow
[`docs/local-environment.md`](docs/local-environment.md).

You need CMake 3.25 or later, Ninja and a C++20 compiler (GCC 13 or later, Clang 18 or later, or
the MSVC of Visual Studio 2022 or 2026). The dependencies come from [vcpkg](https://vcpkg.io) in
manifest mode, with the version pinned in `vcpkg.json` (ADR-10); the `VCPKG_ROOT` variable must
point to vcpkg. `tools/ci/bootstrap-vcpkg.sh` downloads that version.

```sh
cmake --preset dev            # Debug with tests; "release" for Release
cmake --build --preset dev
ctest --preset dev
```

On Windows, open the folder with Visual Studio 2022 (17.5 or later) or 2026 with the
`CMakePresets.json` integration enabled, or use the developer command prompt. The `asan`, `tsan`,
`coverage` and `fuzz` presets are meant for Linux or WSL 2; `tools/ci/ci.sh` runs them the same
way continuous integration does:

```sh
tools/ci/ci.sh asan        # AddressSanitizer and UBSan
tools/ci/ci.sh tsan        # ThreadSanitizer
tools/ci/ci.sh coverage    # coverage; fails below 90 %
tools/ci/ci.sh fuzz        # Clang and libFuzzer
tools/ci/ci.sh all         # everything continuous integration does
```

Outside Windows, vcpkg builds libsodium with autotools: install `autoconf`, `autoconf-archive`,
`automake` and `libtool`.

### Without vcpkg

On Linux you can build with the system libraries. On Ubuntu 26.04 or 24.04:

```sh
sudo apt install libsodium-dev nlohmann-json3-dev libgtest-dev librapidcheck-dev
cmake --preset system && cmake --build --preset system && ctest --preset system
```

### Android

With NDK r28 or later (continuous integration uses r30, the current LTS release) and the
`ANDROID_NDK_HOME` variable:

```sh
cmake --preset android-arm64 && cmake --build --preset android-arm64    # phones
cmake --preset android-x86_64 && cmake --build --preset android-x86_64  # emulator
python3 tools/ci/check_page_alignment.py build/android-arm64/core
```

The shared library exports only the C API functions and is aligned to 16 KB pages, as Google
Play requires for apps that target Android 15 (API 35) or later on 64-bit devices (RNF-20).

## C API

The apps talk to the core through JSON commands. A session is used from a single thread, and
every returned buffer is freed with `mc_buffer_free`:

```c
mc_session* session = NULL;
uint8_t* error = NULL;
size_t error_len = 0;
if (mc_session_create(pack, pack_len, &session, &error, &error_len) != MC_OK) {
    /* error: {"ok":false,"error":"bad_pack","detail":"..."} */
    mc_buffer_free(error);
    return;
}

const char* command = "{\"cmd\":\"validate_deck\",\"deck\":{\"seeker\":\"...\",\"main\":[...]}}";
uint8_t* reply = NULL;
size_t reply_len = 0;
mc_session_dispatch(session, (const uint8_t*)command, strlen(command), &reply, &reply_len);
/* reply: {"ok":true,"valid":false,"issues":[{"rule":"deck_too_small","detail":"..."}]} */
mc_buffer_free(reply);
mc_session_destroy(session);
```

| Command | Reply |
|---|---|
| `{"cmd":"pack_info"}` | `{"ok":true,"pack":{"id","version","date","origin","license","cards"}}` |
| `{"cmd":"validate_deck","deck":{...}}` | `{"ok":true,"valid":bool,"issues":[{"rule","card","detail"}]}` |

The deck has the shape `{"seeker","legacy","terrain","main":[{"card","copies"}],"side":[...]}`.
Each issue carries a stable code that the app translates: `seeker_missing`,
`seeker_wrong_type`, `legacy_missing`, `legacy_wrong_type`, `legacy_mismatch`, `terrain_missing`,
`terrain_wrong_type`, `unknown_card`, `invalid_copies`, `card_type_not_allowed`,
`rank_not_allowed`, `sigil_mismatch`, `too_many_copies`, `deck_too_small`, `deck_too_large` and
`side_too_large`. The `detail` text is in English and meant for debugging, not for players.

## Continuous integration

The workflows in [`.woodpecker/`](.woodpecker) call `tools/ci/ci.sh`, which can also be run
locally. To use no more of Codeberg's resources than needed, the heavy work runs separately:

| Workflow | When | What |
|---|---|---|
| `core.yaml` | On every push and pull request, unless only documentation changes | clang-format and clang-tidy, ASan and UBSan, minimum coverage of 90 % (RNF-17) and dependency licenses |
| `extended.yaml` | Once a week or manually | ThreadSanitizer, Release, fuzzing and the Android library with the 16 KB check (RNF-19 and RNF-20) |
| `licenses.yaml` | SPDX headers on every push; ScanCode once a week or manually | Licenses of the project's own code (RNF-15) |

`core.yaml` and `extended.yaml` use the `ubuntu:26.04` image (GCC 15, Clang 21 and CMake 4.2). For
the weekly run, create a scheduled task (Cron) named `weekly` in Woodpecker.

## Contributing

Code, comments, documentation, scripts and commit messages are written in English. The only
exception is the architecture and requirements document, which stays in Spanish.

## License

The code is free software under the [Apache License 2.0](LICENSE); every file carries its SPDX
header. The dependencies have compatible licenses (ADR-08); see [`NOTICE`](NOTICE). Malediction
and all its content belong to Loot Studios.
