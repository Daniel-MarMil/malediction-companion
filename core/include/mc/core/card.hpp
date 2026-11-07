// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mc::core {

/// Tipos de carta del reglamento.
enum class CardType : std::uint8_t { Seeker, Legacy, Terrain, Unit, Spell, Item, Relic };

/// Rango de una carta; limita sus copias en el mazo.
enum class Rank : std::uint8_t { None, Basic, Elite, Unique, Legendary };

[[nodiscard]] std::optional<CardType> parse_card_type(std::string_view text) noexcept;
[[nodiscard]] std::optional<Rank> parse_rank(std::string_view text) noexcept;
[[nodiscard]] std::string_view to_string(CardType type) noexcept;
[[nodiscard]] std::string_view to_string(Rank rank) noexcept;

/// Una carta tal como la describe un paquete de reglas.
struct Card {
    std::string id;
    std::string name;
    CardType type{CardType::Unit};
    Rank rank{Rank::None};
    int cost{0};
    std::vector<std::string> sigils;  ///< Sigilos de facción; vacío significa carta sin facción.
    std::string seeker_id;            ///< Solo en una Legacy: el Seeker al que pertenece.
    int starting_echo{0};             ///< Solo en una Legacy: el eco inicial.
};

}  // namespace mc::core
