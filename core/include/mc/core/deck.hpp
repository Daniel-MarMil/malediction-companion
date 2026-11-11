// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

namespace mc::core {

/// One deck line: a card and how many copies of it.
struct DeckEntry {
    std::string card_id;
    int copies{1};

    friend bool operator==(const DeckEntry&, const DeckEntry&) = default;
};

/// Warband: Seeker, Legacy and terrain, plus the main deck and the side deck.
struct Deck {
    std::string seeker_id;
    std::string legacy_id;
    std::string terrain_id;
    std::vector<DeckEntry> main;
    std::vector<DeckEntry> side;

    friend bool operator==(const Deck&, const Deck&) = default;
};

}  // namespace mc::core
