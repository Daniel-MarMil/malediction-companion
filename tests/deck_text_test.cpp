// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0
//
// Deck lists as text (RF-09 and RF-10), with the made-up cards of the test pack only. The
// lists follow the shape of a malediction.gg export: Seeker, Legacy and terrain first,
// then "Main Deck" and "Side Deck".

#include "mc/core/deck_text.hpp"
#include "test_support.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace mc::core;
using mc::test::entries;
using mc::test::test_pack;

/// A list like a malediction.gg export, with a comma in the Seeker's name, a typographic
/// apostrophe that the pack writes straight and the other way round, and a side deck.
constexpr std::string_view sample_list =
    "1x Kindra, the Ash Test Seeker\n"
    "1x Ash Test Legacy\n"
    "1x Test Terrain: Plain\n"
    "\n"
    "Main Deck\n"
    "3x Ash Test Unit 1\n"
    "3x Ash Test Unit 2\n"
    "3x Ash Test Unit 3\n"
    "3x Ash Test Unit 4\n"
    "3x Ash Test Unit 5\n"
    "3x Ash Test Unit 6\n"
    "3x Ash Test Spell 1\n"
    "3x Kindra's Test Spell\n"
    "3x Kindra\xE2\x80\x99s Test Ember\n"
    "3x Ash Test Item\n"
    "\n"
    "Side Deck\n"
    "2x Unaligned Test Item\n"
    "2x Unaligned Test Spell";

Deck sample_deck() {
    Deck deck = mc::test::valid_ash_deck();
    deck.side = entries({{"T-NEU-I01", 2}, {"T-NEU-S01", 2}});
    return deck;
}

ImportedDeck import(std::string_view text) {
    return import_deck_text(test_pack(), text);
}

std::vector<std::pair<std::size_t, DeckTextIssue>> errors_of(const ImportedDeck& imported) {
    std::vector<std::pair<std::size_t, DeckTextIssue>> result;
    for (const auto& error : imported.errors) {
        result.emplace_back(error.line, error.issue);
    }
    return result;
}

std::string with_crlf(std::string_view text) {
    std::string result;
    for (const char c : text) {
        if (c == '\n') {
            result += '\r';
        }
        result += c;
    }
    return result;
}

TEST(DeckText, ImportsAMaledictionGgList) {
    const ImportedDeck imported = import(sample_list);
    EXPECT_TRUE(imported.errors.empty());
    EXPECT_FALSE(imported.errors_truncated);
    EXPECT_EQ(imported.deck, sample_deck());
    // It is a valid deck, but that is validate_deck's business.
    EXPECT_TRUE(validate_deck(test_pack(), imported.deck).empty());
}

TEST(DeckText, ExportsTheSameFormat) {
    std::string expected(sample_list);
    // The pack writes these names with the other apostrophe, and export uses the pack's names.
    const auto swap = [&](std::string_view from, std::string_view to) {
        expected.replace(expected.find(from), from.size(), to);
    };
    swap("Kindra's Test Spell", "Kindra\xE2\x80\x99s Test Spell");
    swap("Kindra\xE2\x80\x99s Test Ember", "Kindra's Test Ember");
    EXPECT_EQ(export_deck_text(test_pack(), sample_deck()), expected);
}

TEST(DeckText, ExportThenImportGivesTheSameDeck) {
    const Deck deck = sample_deck();
    const ImportedDeck back = import(export_deck_text(test_pack(), deck));
    EXPECT_TRUE(back.errors.empty());
    EXPECT_EQ(back.deck, deck);
}

TEST(DeckText, AcceptsWindowsLineEndingsAndAByteOrderMark) {
    const ImportedDeck imported = import("\xEF\xBB\xBF" + with_crlf(sample_list) + "\r\n");
    EXPECT_TRUE(imported.errors.empty());
    EXPECT_EQ(imported.deck, sample_deck());
}

TEST(DeckText, IgnoresCaseExtraSpacesAndBlankLines) {
    const ImportedDeck imported = import(
        "  1X   kindra,  the ash TEST seeker  \n"
        "\n\n"
        "1x ash test legacy\t\n"
        "1 x Test\xC2\xA0Terrain: Plain\n"
        "MAIN DECK:\n"
        "\n"
        "03x  ash test unit 1\n"
        "  side deck  \n"
        "2x UNALIGNED TEST ITEM\n");
    EXPECT_TRUE(imported.errors.empty()) << imported.errors.front().detail;
    Deck expected;
    expected.seeker_id = "T-SEEK-ASH";
    expected.legacy_id = "T-LEG-ASH";
    expected.terrain_id = "T-TER-01";
    expected.main = entries({{"T-ASH-U01", 3}});
    expected.side = entries({{"T-NEU-I01", 2}});
    EXPECT_EQ(imported.deck, expected);
}

TEST(DeckText, TheFirstLinesGoByTypeInAnyOrder) {
    const ImportedDeck imported = import("1x Test Terrain: Bramble\n1x Thorn Test Legacy\n1x Thorn Test Seeker\n");
    EXPECT_TRUE(imported.errors.empty());
    EXPECT_EQ(imported.deck.seeker_id, "T-SEEK-THORN");
    EXPECT_EQ(imported.deck.legacy_id, "T-LEG-THORN");
    EXPECT_EQ(imported.deck.terrain_id, "T-TER-02");
}

TEST(DeckText, MergesRepeatedCardsInTheirFirstPlace) {
    const ImportedDeck imported = import(
        "Main Deck\n"
        "1x Ash Test Unit 1\n"
        "2x Ash Test Unit 2\n"
        "2x Ash Test Unit 1\n"
        "Side Deck\n"
        "1x Ash Test Unit 1\n"
        "Main Deck\n"
        "1x Ash Test Unit 2\n");
    EXPECT_TRUE(imported.errors.empty());
    EXPECT_EQ(imported.deck.main, entries({{"T-ASH-U01", 3}, {"T-ASH-U02", 3}}));
    EXPECT_EQ(imported.deck.side, entries({{"T-ASH-U01", 1}}));
}

TEST(DeckText, EmptySectionsAndTextAreFine) {
    EXPECT_EQ(import("").deck, Deck{});
    EXPECT_TRUE(import("\n\r\n  \n").errors.empty());
    const ImportedDeck imported = import("Main Deck\n\nSide Deck\n");
    EXPECT_TRUE(imported.errors.empty());
    EXPECT_EQ(imported.deck, Deck{});
}

TEST(DeckText, ReportsEachBadLineWithItsNumber) {
    const ImportedDeck imported = import(
        "1x Kindra, the Ash Test Seeker\n"  // 1
        "1x Ash Test Unit 1\n"              // 2: not in the first lines
        "1x Thorn Test Seeker\n"            // 3: a second Seeker
        "2x Ash Test Legacy\n"              // 4: 2x before Main Deck
        "\n"                                // 5
        "Main Deck\n"                       // 6
        "3x Not A Card\n"                   // 7
        "Ash Test Unit 2\n"                 // 8: no copies; a card name is not a section title
        "0x Ash Test Unit 3\n"              // 9
        "100x Ash Test Unit 4\n"            // 10
        "3x\n"                              // 11
        "3xAsh Test Unit 5\n"               // 12
        "3x Ash Test Unit 6\n");            // 13: fine
    const std::vector<std::pair<std::size_t, DeckTextIssue>> expected = {
        {2, DeckTextIssue::WrongSection}, {3, DeckTextIssue::WrongSection},      {4, DeckTextIssue::BadCopies},
        {7, DeckTextIssue::UnknownCard},  {8, DeckTextIssue::UnrecognizedLine},  {9, DeckTextIssue::BadCopies},
        {10, DeckTextIssue::BadCopies},   {11, DeckTextIssue::UnrecognizedLine}, {12, DeckTextIssue::UnrecognizedLine}};
    EXPECT_EQ(errors_of(imported), expected);
    // What it did understand is still there.
    EXPECT_EQ(imported.deck.seeker_id, "T-SEEK-ASH");
    EXPECT_TRUE(imported.deck.legacy_id.empty());
    EXPECT_EQ(imported.deck.main, entries({{"T-ASH-U06", 3}}));
}

TEST(DeckText, UnknownSectionsImportNothingUntilTheNextTitle) {
    const ImportedDeck imported = import(
        "1x Kindra, the Ash Test Seeker\n"  // 1
        "\n"                                // 2
        "Main Deck\n"                       // 3
        "3x Ash Test Unit 1\n"              // 4
        "Maybeboard\n"                      // 5: unknown section
        "3x Ash Test Unit 2\n"              // 6: not imported
        "\n"                                // 7: blank lines are still fine
        "3xBroken\n"                        // 8: not imported either
        "Tokens:\n"                         // 9: another unknown section
        "1x Not A Card\n"                   // 10: not imported
        "side deck\n"                       // 11: a known title ends it
        "2x Unaligned Test Item\n");        // 12
    const std::vector<std::pair<std::size_t, DeckTextIssue>> expected = {{5, DeckTextIssue::UnknownSection},
                                                                         {6, DeckTextIssue::UnknownSection},
                                                                         {8, DeckTextIssue::UnknownSection},
                                                                         {9, DeckTextIssue::UnknownSection},
                                                                         {10, DeckTextIssue::UnknownSection}};
    ASSERT_EQ(errors_of(imported), expected);
    // Each line under an unknown section says which title it is under.
    EXPECT_NE(imported.errors[1].detail.find("line 5"), std::string::npos) << imported.errors[1].detail;
    EXPECT_NE(imported.errors[4].detail.find("line 9"), std::string::npos) << imported.errors[4].detail;
    EXPECT_EQ(imported.deck.seeker_id, "T-SEEK-ASH");
    EXPECT_EQ(imported.deck.main, entries({{"T-ASH-U01", 3}}));
    EXPECT_EQ(imported.deck.side, entries({{"T-NEU-I01", 2}}));
}

TEST(DeckText, TextAboveTheFirstLinesIsAnUnknownSectionToo) {
    // A deck name on top hides the Seeker, the Legacy and the terrain until Main Deck.
    const ImportedDeck imported = import(
        "My Ash deck\n"                     // 1: unknown section
        "1x Kindra, the Ash Test Seeker\n"  // 2: not imported
        "Main Deck\n"                       // 3
        "3x Ash Test Unit 1\n");            // 4
    EXPECT_EQ(errors_of(imported), (std::vector<std::pair<std::size_t, DeckTextIssue>>{
                                       {1, DeckTextIssue::UnknownSection}, {2, DeckTextIssue::UnknownSection}}));
    EXPECT_TRUE(imported.deck.seeker_id.empty());
    EXPECT_EQ(imported.deck.main, entries({{"T-ASH-U01", 3}}));
}

TEST(DeckText, MergedLinesCannotGoOver99Copies) {
    const ImportedDeck imported = import("Main Deck\n60x Ash Test Unit 1\n40x Ash Test Unit 1\n39x Ash Test Unit 1\n");
    EXPECT_EQ(errors_of(imported), (std::vector<std::pair<std::size_t, DeckTextIssue>>{{3, DeckTextIssue::BadCopies}}));
    EXPECT_EQ(imported.deck.main, entries({{"T-ASH-U01", 99}}));
}

TEST(DeckText, NamesSharedByTwoCardsAreAmbiguous) {
    auto pack = nlohmann::json::parse(mc::test::test_pack_json());
    pack["cards"].push_back({{"id", "T-COPY"}, {"name", "kindra\xE2\x80\x99s  test spell"}, {"type", "relic"}});
    const RulesPack copy = RulesPack::parse(pack.dump());
    const ImportedDeck imported = import_deck_text(copy, "Main Deck\n1x Kindra's Test Spell\n1x Ash Test Unit 1\n");
    EXPECT_EQ(errors_of(imported),
              (std::vector<std::pair<std::size_t, DeckTextIssue>>{{2, DeckTextIssue::AmbiguousCard}}));
    // Exporting such a card would not read back, so it is refused.
    Deck deck;
    deck.main = entries({{"T-ASH-S02", 1}});
    try {
        (void)export_deck_text(copy, deck);
        ADD_FAILURE() << "expected DeckExportError";
    } catch (const DeckExportError& error) {
        EXPECT_EQ(error.code(), "not_exportable");
    }
}

TEST(DeckText, QuotesLongLinesWithoutCuttingACharacter) {
    // The quote stops at 120 bytes, and byte 120 falls inside the three bytes of ’.
    const std::string line = std::string(119, 'a') + "\xE2\x80\x99" + std::string(10, 'b');
    const ImportedDeck imported = import(line);
    ASSERT_EQ(imported.errors.size(), 1U);
    EXPECT_TRUE(imported.errors[0].detail.ends_with(": " + std::string(119, 'a') + "...")) << imported.errors[0].detail;
}

TEST(DeckText, StopsCollectingErrorsAtTheLimit) {
    std::string text;
    for (std::size_t i = 0; i < max_deck_text_errors + 20; ++i) {
        text += "nonsense\n";
    }
    const ImportedDeck imported = import(text);
    EXPECT_EQ(imported.errors.size(), max_deck_text_errors);
    EXPECT_TRUE(imported.errors_truncated);
}

TEST(DeckText, ASectionHoldsAtMost500DifferentCards) {
    auto pack = nlohmann::json::parse(mc::test::test_pack_json());
    std::string text = "Main Deck\n";
    for (int i = 0; i < 501; ++i) {
        const std::string id = "T-MANY-" + std::to_string(i);
        pack["cards"].push_back({{"id", id}, {"name", "Many " + std::to_string(i)}, {"type", "relic"}});
        text += "1x Many " + std::to_string(i) + "\n";
    }
    const RulesPack many = RulesPack::parse(pack.dump());
    const ImportedDeck imported = import_deck_text(many, text);
    EXPECT_EQ(errors_of(imported),
              (std::vector<std::pair<std::size_t, DeckTextIssue>>{{502, DeckTextIssue::TooManyLines}}));
    EXPECT_EQ(imported.deck.main.size(), 500U);
}

TEST(DeckText, ExportWritesOnlyWhatTheDeckHas) {
    Deck deck;
    deck.main = entries({{"T-ASH-U01", 2}, {"T-ASH-U01", 1}});
    EXPECT_EQ(export_deck_text(test_pack(), deck), "Main Deck\n3x Ash Test Unit 1");
    EXPECT_EQ(export_deck_text(test_pack(), Deck{}), "Main Deck");
    deck.seeker_id = "T-SEEK-THORN";
    EXPECT_EQ(export_deck_text(test_pack(), deck), "1x Thorn Test Seeker\n\nMain Deck\n3x Ash Test Unit 1");
}

TEST(DeckText, ExportRefusesWhatCannotBeWritten) {
    const auto code_of = [](const Deck& deck) {
        try {
            (void)export_deck_text(test_pack(), deck);
        } catch (const DeckExportError& error) {
            return error.code();
        }
        return std::string("exported");
    };
    Deck deck;
    deck.main = entries({{"NOT-IN-PACK", 1}});
    EXPECT_EQ(code_of(deck), "unknown_card");
    deck.main = entries({{"T-ASH-U01", 0}});
    EXPECT_EQ(code_of(deck), "bad_copies");
    deck.main = entries({{"T-ASH-U01", 60}, {"T-ASH-U01", 40}});
    EXPECT_EQ(code_of(deck), "bad_copies");
    deck.main = entries({{"T-ASH-U01", 2147483647}, {"T-ASH-U01", 2147483647}});
    EXPECT_EQ(code_of(deck), "bad_copies");
    deck.main = entries({{"T-ASH-U01", std::numeric_limits<int>::min()}, {"T-ASH-U01", -1}});
    EXPECT_EQ(code_of(deck), "bad_copies");
    deck.main = {};
    deck.seeker_id = "T-ASH-U01";
    EXPECT_EQ(code_of(deck), "wrong_section");
    deck.seeker_id = "NOT-IN-PACK";
    EXPECT_EQ(code_of(deck), "unknown_card");
}

TEST(DeckText, IssueCodesAreStable) {
    EXPECT_EQ(to_string(DeckTextIssue::UnrecognizedLine), "unrecognized_line");
    EXPECT_EQ(to_string(DeckTextIssue::UnknownSection), "unknown_section");
    EXPECT_EQ(to_string(DeckTextIssue::BadCopies), "bad_copies");
    EXPECT_EQ(to_string(DeckTextIssue::UnknownCard), "unknown_card");
    EXPECT_EQ(to_string(DeckTextIssue::AmbiguousCard), "ambiguous_card");
    EXPECT_EQ(to_string(DeckTextIssue::WrongSection), "wrong_section");
    EXPECT_EQ(to_string(DeckTextIssue::TooManyLines), "too_many_lines");
}

}  // namespace
