// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

namespace mc::core {

/// Una línea del mazo: una carta y cuántas copias lleva.
struct DeckEntry {
    std::string card_id;
    int copies{1};
};

/// Warband: Seeker, Legacy y terreno, más el mazo principal y el auxiliar.
struct Deck {
    std::string seeker_id;
    std::string legacy_id;
    std::string terrain_id;
    std::vector<DeckEntry> main;
    std::vector<DeckEntry> side;
};

}  // namespace mc::core
