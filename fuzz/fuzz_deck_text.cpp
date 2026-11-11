// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0
//
// Arbitrary deck lists against the test pack (RF-10 and RNF-19): importing must never crash,
// what it reads must stay within its limits, and exporting what it read must give a list
// that imports back to the same deck.

#include "mc/core/deck_text.hpp"
#include "mc/core/rules_pack.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace mc::core;

const RulesPack& test_pack() {
    static const RulesPack pack = [] {
        std::ifstream file(MC_TEST_PACK_PATH, std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        return RulesPack::parse(text);
    }();
    return pack;
}

void check(bool condition) {
    if (!condition) {
        std::abort();
    }
}

void check_entries(const std::vector<DeckEntry>& entries) {
    check(entries.size() <= 500);
    for (const DeckEntry& entry : entries) {
        check(entry.copies >= 1 && entry.copies <= max_deck_text_copies);
        check(test_pack().find(entry.card_id) != nullptr);
    }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    const ImportedDeck imported = import_deck_text(test_pack(), text);

    check(imported.errors.size() <= max_deck_text_errors);
    check(!imported.errors_truncated || imported.errors.size() == max_deck_text_errors);
    for (const DeckTextError& error : imported.errors) {
        check(error.line >= 1 && error.line <= size + 1);
    }
    check_entries(imported.deck.main);
    check_entries(imported.deck.side);

    // Whatever was read can be written and read back unchanged.
    std::string exported;
    try {
        exported = export_deck_text(test_pack(), imported.deck);
    } catch (const DeckExportError&) {
        std::abort();
    }
    const ImportedDeck back = import_deck_text(test_pack(), exported);
    check(back.errors.empty());
    check(back.deck == imported.deck);
    return 0;
}
