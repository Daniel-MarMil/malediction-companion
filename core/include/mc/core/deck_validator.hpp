// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "mc/core/deck.hpp"
#include "mc/core/rules_pack.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mc::core {

/// A deck-building rule that was broken. The app translates the code; the detail is for debugging.
enum class Rule : std::uint8_t {
    SeekerMissing,
    SeekerWrongType,
    LegacyMissing,
    LegacyWrongType,
    LegacyMismatch,
    TerrainMissing,
    TerrainWrongType,
    UnknownCard,
    InvalidCopies,
    CardTypeNotAllowed,
    RankNotAllowed,
    SigilMismatch,
    TooManyCopies,
    DeckTooSmall,
    DeckTooLarge,
    SideTooLarge,
};

/// Stable code of a rule, for example "deck_too_small".
[[nodiscard]] std::string_view to_string(Rule rule) noexcept;

struct ValidationIssue {
    Rule rule;
    std::string card_id;  ///< Empty if the rule is not about a specific card.
    std::string detail;
};

/// Validates a deck against the pack's rules (RF-06). An empty list means the deck is valid.
[[nodiscard]] std::vector<ValidationIssue> validate_deck(const RulesPack& pack, const Deck& deck);

}  // namespace mc::core
