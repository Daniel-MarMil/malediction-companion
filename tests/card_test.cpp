// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#include "mc/core/card.hpp"
#include "mc/core/deck_validator.hpp"

#include <gtest/gtest.h>

#include <set>
#include <string>

namespace {

using namespace mc::core;

TEST(Card, TypeNamesRoundTrip) {
    for (const CardType type : {CardType::Seeker, CardType::Legacy, CardType::Terrain, CardType::Unit, CardType::Spell,
                                CardType::Item, CardType::Relic}) {
        const auto parsed = parse_card_type(to_string(type));
        ASSERT_TRUE(parsed.has_value()) << to_string(type);
        EXPECT_EQ(*parsed, type);
    }
}

TEST(Card, RankNamesRoundTrip) {
    for (const Rank rank : {Rank::None, Rank::Basic, Rank::Elite, Rank::Unique, Rank::Legendary}) {
        const auto parsed = parse_rank(to_string(rank));
        ASSERT_TRUE(parsed.has_value()) << to_string(rank);
        EXPECT_EQ(*parsed, rank);
    }
}

TEST(Card, UnknownNamesAreRejected) {
    EXPECT_FALSE(parse_card_type("").has_value());
    EXPECT_FALSE(parse_card_type("Unit").has_value()) << "los nombres distinguen mayúsculas";
    EXPECT_FALSE(parse_rank("mythic").has_value());
}

TEST(Rule, CodesAreUniqueAndSnakeCase) {
    std::set<std::string> codes;
    for (int value = static_cast<int>(Rule::SeekerMissing); value <= static_cast<int>(Rule::SideTooLarge); ++value) {
        const std::string code(to_string(static_cast<Rule>(value)));
        EXPECT_NE(code, "unknown");
        EXPECT_EQ(code.find_first_not_of("abcdefghijklmnopqrstuvwxyz_"), std::string::npos) << code;
        EXPECT_TRUE(codes.insert(code).second) << "código repetido: " << code;
    }
    EXPECT_EQ(codes.size(), 16U);
}

}  // namespace
