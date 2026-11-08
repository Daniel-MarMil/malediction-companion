// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mc::core {

/// Card types defined by the rules.
enum class CardType : std::uint8_t { Seeker, Legacy, Terrain, Unit, Spell, Item, Relic };

/// Rank of a card; it limits how many copies a deck may hold.
enum class Rank : std::uint8_t { None, Basic, Elite, Unique, Legendary };

[[nodiscard]] std::optional<CardType> parse_card_type(std::string_view text) noexcept;
[[nodiscard]] std::optional<Rank> parse_rank(std::string_view text) noexcept;
[[nodiscard]] std::string_view to_string(CardType type) noexcept;
[[nodiscard]] std::string_view to_string(Rank rank) noexcept;

/// A card as a rules pack describes it.
struct Card {
    std::string id;
    std::string name;
    CardType type{CardType::Unit};
    Rank rank{Rank::None};
    int cost{0};
    std::vector<std::string> sigils;  ///< Faction sigils; empty means an unaligned card.
    std::string seeker_id;            ///< Legacy only: the Seeker it belongs to.
    int starting_echo{0};             ///< Legacy only: the starting echo.
};

}  // namespace mc::core
