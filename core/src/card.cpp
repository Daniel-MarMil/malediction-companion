// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#include "mc/core/card.hpp"

#include <array>
#include <utility>

namespace mc::core {
namespace {

// Nombres en los paquetes de reglas. Son códigos estables en inglés, como en el reglamento.
constexpr std::array<std::pair<CardType, std::string_view>, 7> card_type_names{{
    {CardType::Seeker, "seeker"},
    {CardType::Legacy, "legacy"},
    {CardType::Terrain, "terrain"},
    {CardType::Unit, "unit"},
    {CardType::Spell, "spell"},
    {CardType::Item, "item"},
    {CardType::Relic, "relic"},
}};

constexpr std::array<std::pair<Rank, std::string_view>, 5> rank_names{{
    {Rank::None, "none"},
    {Rank::Basic, "basic"},
    {Rank::Elite, "elite"},
    {Rank::Unique, "unique"},
    {Rank::Legendary, "legendary"},
}};

}  // namespace

std::optional<CardType> parse_card_type(std::string_view text) noexcept {
    for (const auto& [type, name] : card_type_names) {
        if (name == text) {
            return type;
        }
    }
    return std::nullopt;
}

std::optional<Rank> parse_rank(std::string_view text) noexcept {
    for (const auto& [rank, name] : rank_names) {
        if (name == text) {
            return rank;
        }
    }
    return std::nullopt;
}

std::string_view to_string(CardType type) noexcept {
    for (const auto& [value, name] : card_type_names) {
        if (value == type) {
            return name;
        }
    }
    return "unknown";
}

std::string_view to_string(Rank rank) noexcept {
    for (const auto& [value, name] : rank_names) {
        if (value == rank) {
            return name;
        }
    }
    return "unknown";
}

}  // namespace mc::core
