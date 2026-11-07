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

/// Regla de construcción incumplida. La app traduce el código; el detalle es para depurar.
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

/// Código estable de una regla, por ejemplo "deck_too_small".
[[nodiscard]] std::string_view to_string(Rule rule) noexcept;

struct ValidationIssue {
    Rule rule;
    std::string card_id;  ///< Vacío si la regla no se refiere a una carta concreta.
    std::string detail;
};

/// Valida un mazo con las reglas del paquete (RF-06). Una lista vacía significa mazo válido.
[[nodiscard]] std::vector<ValidationIssue> validate_deck(const RulesPack& pack, const Deck& deck);

}  // namespace mc::core
