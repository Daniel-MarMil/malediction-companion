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

/// The stats printed on a unit card (pack format 2), with the rulebook's names. An empty stat is
/// the card's dash ("–", a null stat): the unit cannot take actions that use it, and it is never
/// 0. Range 0 is different: the unit attacks only nearby enemies.
struct CardStats {
    std::optional<int> accuracy;
    std::optional<int> power_hit;    ///< Power on a hit.
    std::optional<int> power_graze;  ///< Power on a graze.
    std::optional<int> range;
    std::optional<int> speed;
    std::optional<int> defense;
    std::optional<int> max_health;  ///< "Max Health"; the match tracks the current health.

    friend bool operator==(const CardStats&, const CardStats&) = default;
};

/// The card image: a file that travels with the pack (pack format 2). The pack holds its
/// SHA-256, so the pack signature also covers the image.
struct CardImage {
    std::string file;    ///< Path relative to the pack, for example "images/some-card.jpg".
    std::string sha256;  ///< 64 lowercase hexadecimal digits.
    std::string source;  ///< Where the importer downloaded it from.
};

/// A card as a rules pack describes it.
struct Card {
    std::string id;
    std::string name;
    CardType type{CardType::Unit};
    Rank rank{Rank::None};
    std::optional<int> cost;                ///< Empty while the pack does not give it.
    std::vector<std::string> sigils;        ///< Faction sigils; empty means an unaligned card.
    std::string seeker_id;                  ///< Legacy only: the Seeker it belongs to.
    int starting_echo{0};                   ///< Legacy only: the starting echo.
    std::vector<std::string> traits;        ///< In the order the card shows them.
    std::vector<std::string> abilities;     ///< In the order the card shows them.
    std::optional<CardStats> stats;         ///< Units only.
    std::optional<std::string> rules_text;  ///< Empty while the pack does not give it.
    std::optional<CardImage> image;
    std::string source;  ///< The page the card was read from; empty if the pack does not say.
};

}  // namespace mc::core
