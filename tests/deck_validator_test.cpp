// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#include "mc/core/deck_validator.hpp"
#include "test_support.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>

namespace {

using namespace mc::core;
using mc::test::count_rule;
using mc::test::entries;
using mc::test::has_issue;
using mc::test::test_pack;
using mc::test::valid_ash_deck;

std::vector<ValidationIssue> validate(const Deck& deck) {
    return validate_deck(test_pack(), deck);
}

/// El paquete de prueba con otras reglas de mazo.
RulesPack pack_with_rules(const char* key, bool value) {
    auto json = nlohmann::json::parse(mc::test::test_pack_json());
    json["rules"][key] = value;
    return RulesPack::parse(json.dump());
}

TEST(DeckValidator, AcceptsAValidDeck) {
    const auto issues = validate(valid_ash_deck());
    EXPECT_TRUE(issues.empty()) << to_string(issues.front().rule) << ": " << issues.front().detail;
}

TEST(DeckValidator, ReportsAnEmptyDeck) {
    const auto issues = validate(Deck{});
    EXPECT_TRUE(has_issue(issues, Rule::SeekerMissing));
    EXPECT_TRUE(has_issue(issues, Rule::LegacyMissing));
    EXPECT_TRUE(has_issue(issues, Rule::TerrainMissing));
    EXPECT_TRUE(has_issue(issues, Rule::DeckTooSmall));
    EXPECT_EQ(issues.size(), 4U);
}

TEST(DeckValidator, ChecksTheSeeker) {
    Deck deck = valid_ash_deck();
    deck.seeker_id = "T-ASH-U01";
    auto issues = validate(deck);
    EXPECT_TRUE(has_issue(issues, Rule::SeekerWrongType, "T-ASH-U01"));
    // Sin Seeker válido no se comparan sigilos ni la Legacy.
    EXPECT_FALSE(has_issue(issues, Rule::SigilMismatch));
    EXPECT_FALSE(has_issue(issues, Rule::LegacyMismatch));

    deck.seeker_id = "T-SEEK-NO-EXISTE";
    issues = validate(deck);
    EXPECT_TRUE(has_issue(issues, Rule::UnknownCard, "T-SEEK-NO-EXISTE"));
}

TEST(DeckValidator, ChecksTheLegacy) {
    Deck deck = valid_ash_deck();
    deck.legacy_id.clear();
    EXPECT_TRUE(has_issue(validate(deck), Rule::LegacyMissing));

    deck.legacy_id = "T-TER-01";
    EXPECT_TRUE(has_issue(validate(deck), Rule::LegacyWrongType, "T-TER-01"));

    deck.legacy_id = "T-LEG-THORN";
    EXPECT_TRUE(has_issue(validate(deck), Rule::LegacyMismatch, "T-LEG-THORN"));

    deck.legacy_id = "T-LEG-NO-EXISTE";
    EXPECT_TRUE(has_issue(validate(deck), Rule::UnknownCard, "T-LEG-NO-EXISTE"));
}

TEST(DeckValidator, ChecksTheTerrain) {
    Deck deck = valid_ash_deck();
    deck.terrain_id.clear();
    EXPECT_TRUE(has_issue(validate(deck), Rule::TerrainMissing));

    deck.terrain_id = "T-ASH-U01";
    EXPECT_TRUE(has_issue(validate(deck), Rule::TerrainWrongType, "T-ASH-U01"));

    // El terreno puede ser de otra facción.
    deck.terrain_id = "T-TER-02";
    EXPECT_TRUE(validate(deck).empty());
}

TEST(DeckValidator, ChecksTheDeckSize) {
    Deck deck = valid_ash_deck();
    deck.main.back().copies = 2;  // 29 cartas
    EXPECT_TRUE(has_issue(validate(deck), Rule::DeckTooSmall));

    deck = valid_ash_deck();
    deck.seeker_id = "T-SEEK-DUAL";
    deck.legacy_id = "T-LEG-DUAL";
    const auto extra = entries({{"T-NEU-I01", 3},
                                {"T-NEU-S01", 3},
                                {"T-ASH-E01", 2},
                                {"T-ASH-E02", 2},
                                {"T-ASH-Q01", 1},
                                {"T-TIDE-U01", 3},
                                {"T-TIDE-U02", 3},
                                {"T-TIDE-E01", 1},
                                {"T-DUAL-E01", 2}});  // 20 más: 50 cartas, el máximo
    deck.main.insert(deck.main.end(), extra.begin(), extra.end());
    EXPECT_TRUE(validate(deck).empty());

    deck.main.push_back({"T-TIDE-U03", 1});  // 51 cartas
    const auto issues = validate(deck);
    EXPECT_TRUE(has_issue(issues, Rule::DeckTooLarge));
    EXPECT_EQ(issues.size(), 1U);
}

TEST(DeckValidator, ChecksTheSideDeck) {
    Deck deck = valid_ash_deck();
    deck.side = entries({{"T-ASH-E01", 2}, {"T-ASH-E02", 2}, {"T-ASH-Q01", 1}, {"T-NEU-I01", 2}});  // 7
    EXPECT_TRUE(validate(deck).empty());

    deck.side.push_back({"T-NEU-S01", 1});  // 8
    EXPECT_TRUE(has_issue(validate(deck), Rule::SideTooLarge));
}

TEST(DeckValidator, LimitsCopiesByRank) {
    Deck deck = valid_ash_deck();
    deck.main.front().copies = 4;
    EXPECT_TRUE(has_issue(validate(deck), Rule::TooManyCopies, "T-ASH-U01"));

    deck = valid_ash_deck();
    deck.main.push_back({"T-ASH-E01", 3});
    EXPECT_TRUE(has_issue(validate(deck), Rule::TooManyCopies, "T-ASH-E01"));

    deck = valid_ash_deck();
    deck.main.push_back({"T-ASH-Q01", 2});
    EXPECT_TRUE(has_issue(validate(deck), Rule::TooManyCopies, "T-ASH-Q01"));
}

TEST(DeckValidator, AddsUpRepeatedLines) {
    Deck deck = valid_ash_deck();
    deck.main.push_back({"T-ASH-U01", 1});
    deck.main.push_back({"T-ASH-U01", 1});
    const auto issues = validate(deck);
    EXPECT_EQ(count_rule(issues, Rule::TooManyCopies), 1U);
    EXPECT_TRUE(has_issue(issues, Rule::TooManyCopies, "T-ASH-U01"));
}

TEST(DeckValidator, CountsTheSideDeckForCopyLimits) {
    Deck deck = valid_ash_deck();
    deck.side = entries({{"T-ASH-U01", 1}});
    EXPECT_TRUE(has_issue(validate(deck), Rule::TooManyCopies, "T-ASH-U01"));

    const RulesPack separate = pack_with_rules("copy_limits_include_side", false);
    EXPECT_TRUE(validate_deck(separate, deck).empty());

    deck.side = entries({{"T-ASH-U01", 4}});
    EXPECT_TRUE(has_issue(validate_deck(separate, deck), Rule::TooManyCopies, "T-ASH-U01"));
}

TEST(DeckValidator, RejectsCardsThatDoNotGoInTheDeck) {
    for (const char* id : {"T-REL-01", "T-SEEK-ASH", "T-LEG-ASH", "T-TER-01"}) {
        Deck deck = valid_ash_deck();
        deck.main.push_back({id, 1});
        EXPECT_TRUE(has_issue(validate(deck), Rule::CardTypeNotAllowed, id)) << id;
    }
}

TEST(DeckValidator, RejectsLegendaryCards) {
    Deck deck = valid_ash_deck();
    deck.main.push_back({"T-ASH-L01", 1});
    const auto issues = validate(deck);
    EXPECT_TRUE(has_issue(issues, Rule::RankNotAllowed, "T-ASH-L01"));
    EXPECT_FALSE(has_issue(issues, Rule::TooManyCopies));
}

TEST(DeckValidator, ChecksSigils) {
    Deck deck = valid_ash_deck();
    deck.main.push_back({"T-TIDE-U01", 1});
    deck.main.push_back({"T-DUAL-E01", 1});
    auto issues = validate(deck);
    EXPECT_TRUE(has_issue(issues, Rule::SigilMismatch, "T-TIDE-U01"));
    EXPECT_TRUE(has_issue(issues, Rule::SigilMismatch, "T-DUAL-E01"));

    // Un Seeker de dos facciones admite las cartas de cada una y las de ambas.
    deck.seeker_id = "T-SEEK-DUAL";
    deck.legacy_id = "T-LEG-DUAL";
    EXPECT_TRUE(validate(deck).empty());

    deck.main.push_back({"T-THORN-U01", 1});
    issues = validate(deck);
    EXPECT_TRUE(has_issue(issues, Rule::SigilMismatch, "T-THORN-U01"));
    EXPECT_EQ(issues.size(), 1U);
}

TEST(DeckValidator, UnalignedCardsDependOnTheRules) {
    Deck deck = valid_ash_deck();
    deck.main.push_back({"T-NEU-I01", 1});
    EXPECT_TRUE(validate(deck).empty());

    const RulesPack strict = pack_with_rules("unaligned_cards_allowed", false);
    EXPECT_TRUE(has_issue(validate_deck(strict, deck), Rule::SigilMismatch, "T-NEU-I01"));
}

TEST(DeckValidator, ReportsInvalidCopiesAndUnknownCards) {
    Deck deck = valid_ash_deck();
    deck.main.push_back({"T-ASH-E01", 0});
    deck.main.push_back({"T-ASH-E02", -3});
    deck.main.push_back({"NO-EXISTE", 1});
    deck.main.push_back({"NO-EXISTE", 1});
    const auto issues = validate(deck);
    EXPECT_TRUE(has_issue(issues, Rule::InvalidCopies, "T-ASH-E01"));
    EXPECT_TRUE(has_issue(issues, Rule::InvalidCopies, "T-ASH-E02"));
    EXPECT_EQ(count_rule(issues, Rule::UnknownCard), 1U);
}

TEST(DeckValidator, KeepsALargeCopyCountWithoutOverflow) {
    Deck deck = valid_ash_deck();
    deck.main.push_back({"T-ASH-E01", 2147483647});
    deck.main.push_back({"T-ASH-E01", 2147483647});
    const auto issues = validate(deck);
    EXPECT_TRUE(has_issue(issues, Rule::TooManyCopies, "T-ASH-E01"));
    EXPECT_TRUE(has_issue(issues, Rule::DeckTooLarge));
}

TEST(DeckValidator, ExplainsEachIssue) {
    Deck deck = valid_ash_deck();
    deck.main.front().copies = 4;
    for (const auto& issue : validate(deck)) {
        EXPECT_FALSE(issue.detail.empty()) << to_string(issue.rule);
    }
}

}  // namespace
