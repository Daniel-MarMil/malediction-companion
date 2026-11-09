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

/// The detail of the first issue with that rule, or an empty text if there is none.
std::string detail_of(const std::vector<ValidationIssue>& issues, Rule rule) {
    for (const auto& issue : issues) {
        if (issue.rule == rule) {
            return issue.detail;
        }
    }
    return {};
}

/// The test pack with different deck rules.
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
    // Without a valid Seeker, neither the sigils nor the Legacy are compared.
    EXPECT_FALSE(has_issue(issues, Rule::SigilMismatch));
    EXPECT_FALSE(has_issue(issues, Rule::LegacyMismatch));

    deck.seeker_id = "T-SEEK-UNKNOWN";
    issues = validate(deck);
    EXPECT_TRUE(has_issue(issues, Rule::UnknownCard, "T-SEEK-UNKNOWN"));
}

TEST(DeckValidator, ChecksTheLegacy) {
    Deck deck = valid_ash_deck();
    deck.legacy_id.clear();
    EXPECT_TRUE(has_issue(validate(deck), Rule::LegacyMissing));

    deck.legacy_id = "T-TER-01";
    EXPECT_TRUE(has_issue(validate(deck), Rule::LegacyWrongType, "T-TER-01"));

    deck.legacy_id = "T-LEG-THORN";
    EXPECT_TRUE(has_issue(validate(deck), Rule::LegacyMismatch, "T-LEG-THORN"));

    deck.legacy_id = "T-LEG-UNKNOWN";
    EXPECT_TRUE(has_issue(validate(deck), Rule::UnknownCard, "T-LEG-UNKNOWN"));
}

TEST(DeckValidator, ChecksTheTerrain) {
    Deck deck = valid_ash_deck();
    deck.terrain_id.clear();
    EXPECT_TRUE(has_issue(validate(deck), Rule::TerrainMissing));

    deck.terrain_id = "T-ASH-U01";
    EXPECT_TRUE(has_issue(validate(deck), Rule::TerrainWrongType, "T-ASH-U01"));

    // The terrain can belong to another faction.
    deck.terrain_id = "T-TER-02";
    EXPECT_TRUE(validate(deck).empty());
}

TEST(DeckValidator, ChecksTheDeckSize) {
    Deck deck = valid_ash_deck();
    deck.main.back().copies = 2;  // 29 cards
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
                                {"T-DUAL-E01", 2}});  // 20 more: 50 cards, the maximum
    deck.main.insert(deck.main.end(), extra.begin(), extra.end());
    EXPECT_TRUE(validate(deck).empty());

    deck.main.push_back({"T-TIDE-U03", 1});  // 51 cards
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

    // A Seeker with two factions allows the cards of each faction and the cards of both.
    deck.seeker_id = "T-SEEK-DUAL";
    deck.legacy_id = "T-LEG-DUAL";
    EXPECT_TRUE(validate(deck).empty());

    deck.main.push_back({"T-THORN-U01", 1});
    issues = validate(deck);
    EXPECT_TRUE(has_issue(issues, Rule::SigilMismatch, "T-THORN-U01"));
    EXPECT_TRUE(has_issue(issues, Rule::DeckHasInvalidCards));
    EXPECT_EQ(issues.size(), 2U);
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
    deck.main.push_back({"NOT-IN-PACK", 1});
    deck.main.push_back({"NOT-IN-PACK", 1});
    const auto issues = validate(deck);
    EXPECT_TRUE(has_issue(issues, Rule::InvalidCopies, "T-ASH-E01"));
    EXPECT_TRUE(has_issue(issues, Rule::InvalidCopies, "T-ASH-E02"));
    EXPECT_EQ(count_rule(issues, Rule::UnknownCard), 1U);
}

TEST(DeckValidator, CountsCardsThatShouldNotBeThereAndMarksTheDeck) {
    // 27 valid cards and 3 that should not be in the deck: 30 cards, so the size is right,
    // but the deck is not valid.
    Deck deck = valid_ash_deck();
    deck.main.pop_back();
    deck.main.push_back({"NOT-IN-PACK", 1});
    deck.main.push_back({"T-REL-01", 1});
    deck.main.push_back({"T-TIDE-U01", 1});
    auto issues = validate(deck);
    EXPECT_TRUE(has_issue(issues, Rule::UnknownCard, "NOT-IN-PACK"));
    EXPECT_TRUE(has_issue(issues, Rule::CardTypeNotAllowed, "T-REL-01"));
    EXPECT_TRUE(has_issue(issues, Rule::SigilMismatch, "T-TIDE-U01"));
    EXPECT_TRUE(has_issue(issues, Rule::DeckHasInvalidCards));
    EXPECT_FALSE(has_issue(issues, Rule::DeckTooSmall));
    EXPECT_EQ(issues.size(), 4U);
    EXPECT_EQ(detail_of(issues, Rule::DeckHasInvalidCards),
              "cards that should not be in the deck: 3 of 30 in the main deck");

    // Without them, the deck is too small and has nothing else wrong.
    deck.main.resize(9);
    issues = validate(deck);
    EXPECT_TRUE(has_issue(issues, Rule::DeckTooSmall));
    EXPECT_EQ(issues.size(), 1U);
}

TEST(DeckValidator, CountsEveryCopyOfACardThatShouldNotBeThere) {
    Deck deck = valid_ash_deck();
    deck.main.push_back({"NOT-IN-PACK", 2});
    deck.main.push_back({"NOT-IN-PACK", 3});
    const auto issues = validate(deck);
    EXPECT_EQ(count_rule(issues, Rule::UnknownCard), 1U);
    EXPECT_EQ(count_rule(issues, Rule::DeckHasInvalidCards), 1U);
    EXPECT_EQ(detail_of(issues, Rule::DeckHasInvalidCards),
              "cards that should not be in the deck: 5 of 35 in the main deck");
}

TEST(DeckValidator, MarksTheDeckForCardsInTheSideDeck) {
    Deck deck = valid_ash_deck();
    deck.side = entries({{"T-ASH-L01", 1}, {"T-NEU-I01", 2}});
    auto issues = validate(deck);
    EXPECT_TRUE(has_issue(issues, Rule::RankNotAllowed, "T-ASH-L01"));
    EXPECT_EQ(detail_of(issues, Rule::DeckHasInvalidCards),
              "cards that should not be in the deck: 1 of 3 in the side deck");

    deck.main.push_back({"T-SEEK-ASH", 1});
    issues = validate(deck);
    EXPECT_EQ(count_rule(issues, Rule::DeckHasInvalidCards), 1U);
    EXPECT_EQ(detail_of(issues, Rule::DeckHasInvalidCards),
              "cards that should not be in the deck: 1 of 31 in the main deck, 1 of 3 in the side deck");
}

TEST(DeckValidator, DoesNotMarkTheDeckForOtherProblems) {
    // Lines without copies, too many copies or a missing slot are problems of their own, not
    // cards that should not be in the deck.
    Deck deck = valid_ash_deck();
    deck.main.push_back({"T-ASH-E01", 0});
    deck.main.front().copies = 4;
    deck.terrain_id.clear();
    const auto issues = validate(deck);
    EXPECT_TRUE(has_issue(issues, Rule::InvalidCopies, "T-ASH-E01"));
    EXPECT_TRUE(has_issue(issues, Rule::TooManyCopies, "T-ASH-U01"));
    EXPECT_FALSE(has_issue(issues, Rule::DeckHasInvalidCards));

    // Without a valid Seeker the sigils cannot be compared, so they do not mark the deck.
    Deck no_seeker = valid_ash_deck();
    no_seeker.seeker_id.clear();
    no_seeker.main.push_back({"T-TIDE-U01", 1});
    EXPECT_FALSE(has_issue(validate(no_seeker), Rule::DeckHasInvalidCards));
}

TEST(DeckValidator, MarksTheDeckForUnalignedCardsWhenTheRulesForbidThem) {
    Deck deck = valid_ash_deck();
    deck.main.push_back({"T-NEU-I01", 2});
    const RulesPack strict = pack_with_rules("unaligned_cards_allowed", false);
    EXPECT_EQ(detail_of(validate_deck(strict, deck), Rule::DeckHasInvalidCards),
              "cards that should not be in the deck: 2 of 32 in the main deck");
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
