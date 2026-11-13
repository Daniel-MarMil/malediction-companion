<!--
SPDX-FileCopyrightText: 2026 Daniel Martín Millet
SPDX-License-Identifier: Apache-2.0
-->

# Seekers Companion

A digital companion for games of [Malediction](https://malediction.gg), the card game with
miniatures by Loot Studios: decks that follow the rules, echo and mastery counters kept in sync
between phones, and a match log. It takes the flow of the Warhammer 40,000 app as a reference,
adapted to a card game. As in Seekers League, each player's decks and matches are kept on a
server, with a copy on the phone to play without a network (ADR-13).

**Unofficial project.** It is not affiliated with or endorsed by Loot Studios.

## Status

Phase 0: the shared C++20 core.

- Versioned rules packs (`mc-rules-pack` format 1) with a detached Ed25519 signature, checked
  against a given key or against the keys compiled into the core (ADR-06).
- Deck validation (RF-06): Seeker, Legacy and terrain; 30 to 50 cards; sigils; copies per rank;
  a side deck of 7. The limits come from the pack, not from the code.
- Deck lists as text (RF-09 and RF-10): import the list that malediction.gg exports, with an
  error for each line it does not understand, and export a deck in the same format.
- C API for the apps (ADR-11), and `mc_cli`, a command-line client to try it by hand.
- `mc_pack_tool`, the development tool that creates the pack keys and signs packs.
- Tests with GoogleTest and RapidCheck, sanitizers, coverage and fuzzing with libFuzzer.

The Android app (Kotlin with Jetpack Compose) and the first server modules come next, and then
the iOS app (Swift with SwiftUI). The iOS app needs a Mac with Xcode; until there is one, work continues on Android.

[`docs/code-tour.md`](docs/code-tour.md) explains how the code is organized, how a command flows
through the core, what is tested and how to try it. Requirement IDs (RF-xx for functional and
RNF-xx for non-functional requirements) and the phases refer to the project's architecture and
requirements document, which is written in Spanish.

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
  include/mc/core/      Domain types: cards, rules pack, deck, validation, deck lists
  src/                  Implementation
tests/                Tests (GoogleTest, RapidCheck) and the C11 header check
examples/             mc_cli, a command-line client for the C API, and example commands
fuzz/                 libFuzzer targets and seed corpus
content/test/         Made-up rules pack for the tests
content/official/     Private official content (ignored by git)
docs/                 Architecture decisions (adr/), code tour and local environment guide
tools/ci/             Local checks (tools/ci/ci.sh) and the scripts they use
tools/pack/           mc_pack_tool: pack keys and signatures (development only)
keys/                 Public development key, dev.pub (ADR-06)
tools/dev/            Local environment setup
```

## Building

To set up the full environment on Windows (WSL 2, VS Code, Android Studio and, optionally,
MSVC), follow [`docs/local-environment.md`](docs/local-environment.md).

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
`coverage` and `fuzz` presets are meant for Linux or WSL 2; `tools/ci/ci.sh` runs them with the
project's checks:

```sh
tools/ci/ci.sh asan        # AddressSanitizer and UBSan
tools/ci/ci.sh tsan        # ThreadSanitizer
tools/ci/ci.sh coverage    # coverage; fails below 90 %
tools/ci/ci.sh fuzz        # Clang and libFuzzer
tools/ci/ci.sh all         # every check, ScanCode included
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

With NDK r28 or later (`tools/ci/ci.sh` uses r30, the current LTS release) and the
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
every returned buffer is freed with `mc_buffer_free`. Before creating a session, the app checks
the pack's detached signature with `mc_pack_verify_trusted`, against the keys compiled into the
core:

```c
if (mc_pack_verify_trusted(pack, pack_len, signature, signature_len) != MC_OK) {
    return; /* not signed by a trusted key */
}

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
| `{"cmd":"import_deck","text":"..."}` | `{"ok":true,"deck":{...},"errors":[{"line","error","detail"}]}` |
| `{"cmd":"export_deck","deck":{...}}` | `{"ok":true,"text":"..."}` |

The deck has the shape `{"seeker","legacy","terrain","main":[{"card","copies"}],"side":[...]}`.
Each issue carries a stable code that the app translates: `seeker_missing`,
`seeker_invalid`, `legacy_missing`, `legacy_wrong_type`, `legacy_mismatch`, `terrain_missing`,
`terrain_wrong_type`, `unknown_card`, `invalid_copies`, `card_type_not_allowed`,
`rank_not_allowed`, `sigil_mismatch`, `too_many_copies`, `deck_has_invalid_cards`,
`deck_too_small`, `deck_too_large` and `side_too_large`. Without a valid Seeker the rest of the
deck is not checked, so the only issue is `seeker_missing` or `seeker_invalid`. Cards that should
not be in the deck still count towards its size: each one gets its own issue, and the deck also gets
`deck_has_invalid_cards`. The `detail` text is in English and meant for debugging, not for
players.

`import_deck` reads a deck list in the format that malediction.gg exports (the Seeker, the Legacy
and the terrain with `1x`, then `Main Deck` and `Side Deck` with one `Nx Name` per line) and
returns the deck with the card ids of the pack, in the shape that `validate_deck` takes. It does
not check the deck rules; that is still the job of `validate_deck`. Each line it cannot use gets
an error with its number, counting from 1, and a stable code: `unrecognized_line`,
`unknown_section`, `bad_copies`, `unknown_card`, `ambiguous_card`, `wrong_section` or
`too_many_lines`. A section title other than `Main Deck` or `Side Deck` gets `unknown_section`,
and so does every line under it up to the next known title: none of those cards is imported, so
that no card ends up in the wrong section. The rest of the list is still read, and after 100
errors the reply stops listing them and adds `"errors_truncated":true`.
`export_deck` writes a deck in the same format with the names of the pack, so that importing the
text gives the same deck. A deck that cannot be written that way gets `MC_ERR_BAD_COMMAND` with
`unknown_card`, `wrong_section`, `bad_copies` or `not_exportable`. The code tour has the details
of the format.

| Function | What it does |
|---|---|
| `mc_version` | Core version |
| `mc_pack_verify` | Checks a pack's signature against a given public key |
| `mc_pack_verify_trusted` | Checks a pack's signature against the keys compiled into the core |
| `mc_session_create`, `mc_session_destroy` | Open and close a session with a pack |
| `mc_session_dispatch` | Runs a JSON command |
| `mc_buffer_free` | Frees a buffer returned by the core |

`mc_cli` runs these commands from the command line, for example:

```sh
build/dev/examples/mc_cli content/test/rules-pack.test.json examples/commands/invalid-deck.json
```

## Pack keys

Packs are signed with Ed25519 keys that `mc_pack_tool` creates and uses (ADR-06). It is built
with the core on Linux and WSL, at `$MC_BUILD_ROOT/<preset>/tools/pack/mc_pack_tool`:

```sh
mc_pack_tool keygen dev                       # ~/.config/seekers-companion/keys/dev.mcsk and dev.pub
mc_pack_tool sign ~/.config/seekers-companion/keys/dev.mcsk pack.json   # writes pack.json.sig
mc_pack_tool verify ~/.config/seekers-companion/keys/dev.pub pack.json
```

Passwords are read only from the terminal. The secret key is encrypted with a key derived from
the password with Argon2id (1 GiB of memory, a few seconds), never leaves your machine and never
goes into the repository; keep an offline backup of it and its password.

The core trusts only the public keys compiled into it. Release builds trust only release keys,
which do not exist yet. Debug builds also trust the development key in `keys/dev.pub`
(`MC_TRUST_DEV_KEY`, on by default only in Debug and refused in other build types): after
`keygen dev`, copy `dev.pub` there.

## Checks

There is no remote continuous integration: the checks run locally with `tools/ci/ci.sh`, on
Ubuntu 26.04 (GCC 15, Clang 21 and CMake 4.2), and `tools/ci/ci.sh all` must pass before a commit.

| Task | What |
|---|---|
| `lint` | clang-format and clang-tidy |
| `asan`, `tsan`, `release` | The tests with AddressSanitizer and UBSan, with ThreadSanitizer, and in Release |
| `coverage` | Minimum line coverage of 90 % of `core/src` (RNF-17) |
| `fuzz` | The libFuzzer targets (RNF-19) |
| `android` | The Android library with the 16 KB check (RNF-20) |
| `licenses` | SPDX headers and the licenses of the vcpkg dependencies (ADR-08 and RNF-15) |
| `scancode` | ScanCode Toolkit finds no GPL, LGPL or AGPL in the project's own code (ADR-08 and RNF-15) |

## Contributing

Code, comments, documentation, scripts and commit messages are written in English. The only
exception is the architecture and requirements document, which stays in Spanish.

## License

The code is free software under the [Apache License 2.0](LICENSE); every file carries its SPDX
header. The dependencies have compatible licenses (ADR-08); see [`NOTICE`](NOTICE). Malediction
and all its content belong to Loot Studios.
