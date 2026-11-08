// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#include "mc/core/rules_pack.hpp"
#include "test_support.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace {

using namespace mc::core;
using nlohmann::json;

/// A minimal valid pack; each test breaks it in one way.
json minimal_pack() {
    return json::parse(R"({
        "format": "mc-rules-pack", "format_version": 1,
        "id": "mini", "version": "1", "date": "2026-11-07", "origin": "tests", "license": "Apache-2.0",
        "rules": {"deck_min": 30, "deck_max": 50, "side_max": 7,
                  "copy_limits": {"basic": 3, "elite": 2, "unique": 1}},
        "cards": [
            {"id": "S", "name": "Seeker", "type": "seeker", "rank": "legendary", "sigils": ["ash"]},
            {"id": "L", "name": "Legacy", "type": "legacy", "seeker": "S", "starting_echo": 4},
            {"id": "U", "name": "Unit", "type": "unit", "rank": "basic", "cost": 2, "sigils": ["ash"]}
        ]
    })");
}

void expect_rejected(const json& pack, const std::string& fragment) {
    try {
        (void)RulesPack::parse(pack.dump());
        ADD_FAILURE() << "expected a PackError containing: " << fragment;
    } catch (const PackError& error) {
        EXPECT_NE(std::string(error.what()).find(fragment), std::string::npos) << error.what();
    }
}

TEST(RulesPack, ReadsTheTestPack) {
    const RulesPack& pack = mc::test::test_pack();
    EXPECT_EQ(pack.info().id, "mc-test");
    EXPECT_EQ(pack.info().version, "0.1.0");
    EXPECT_EQ(pack.info().license, "Apache-2.0");
    EXPECT_EQ(pack.card_count(), 35U);

    const DeckRules& rules = pack.deck_rules();
    EXPECT_EQ(rules.deck_min, 30);
    EXPECT_EQ(rules.deck_max, 50);
    EXPECT_EQ(rules.side_max, 7);
    EXPECT_EQ(rules.copy_limits.at(Rank::Basic), 3);
    EXPECT_EQ(rules.copy_limits.at(Rank::Elite), 2);
    EXPECT_EQ(rules.copy_limits.at(Rank::Unique), 1);
    EXPECT_EQ(rules.copy_limits.count(Rank::Legendary), 0U);
    EXPECT_TRUE(rules.unaligned_cards_allowed);
    EXPECT_TRUE(rules.copy_limits_include_side);
}

TEST(RulesPack, FindsCardsById) {
    const RulesPack& pack = mc::test::test_pack();
    const Card* legacy = pack.find("T-LEG-ASH");
    ASSERT_NE(legacy, nullptr);
    EXPECT_EQ(legacy->type, CardType::Legacy);
    EXPECT_EQ(legacy->seeker_id, "T-SEEK-ASH");
    EXPECT_EQ(legacy->starting_echo, 4);

    const Card* unit = pack.find("T-ASH-E01");
    ASSERT_NE(unit, nullptr);
    EXPECT_EQ(unit->rank, Rank::Elite);
    EXPECT_EQ(unit->cost, 4);

    EXPECT_EQ(pack.find("NOT-IN-PACK"), nullptr);
    EXPECT_EQ(pack.find(""), nullptr);
}

TEST(RulesPack, SortsAndDeduplicatesSigils) {
    json pack = minimal_pack();
    pack["cards"][0]["sigils"] = json::array({"tide", "ash", "tide"});
    const RulesPack parsed = RulesPack::parse(pack.dump());
    EXPECT_EQ(parsed.find("S")->sigils, (std::vector<std::string>{"ash", "tide"}));
}

TEST(RulesPack, AppliesDefaults) {
    json pack = minimal_pack();
    pack["cards"].push_back({{"id", "R"}, {"name", "Relic"}, {"type", "relic"}});
    const RulesPack parsed = RulesPack::parse(pack.dump());
    const Card* relic = parsed.find("R");
    ASSERT_NE(relic, nullptr);
    EXPECT_EQ(relic->rank, Rank::None);
    EXPECT_EQ(relic->cost, 0);
    EXPECT_TRUE(relic->sigils.empty());
    EXPECT_TRUE(parsed.deck_rules().unaligned_cards_allowed);
    EXPECT_TRUE(parsed.deck_rules().copy_limits_include_side);
}

TEST(RulesPack, ReadsRuleSwitches) {
    json pack = minimal_pack();
    pack["rules"]["unaligned_cards_allowed"] = false;
    pack["rules"]["copy_limits_include_side"] = false;
    const RulesPack parsed = RulesPack::parse(pack.dump());
    EXPECT_FALSE(parsed.deck_rules().unaligned_cards_allowed);
    EXPECT_FALSE(parsed.deck_rules().copy_limits_include_side);
}

TEST(RulesPack, RejectsInvalidJson) {
    EXPECT_THROW((void)RulesPack::parse(""), PackError);
    EXPECT_THROW((void)RulesPack::parse("{"), PackError);
    EXPECT_THROW((void)RulesPack::parse("[]"), PackError);
    EXPECT_THROW((void)RulesPack::parse("\"\xff\""), PackError);
}

TEST(RulesPack, RejectsWrongFormat) {
    json pack = minimal_pack();
    pack["format"] = "other";
    expect_rejected(pack, "format");

    pack = minimal_pack();
    pack["format_version"] = 2;
    expect_rejected(pack, "format_version");

    pack = minimal_pack();
    pack["format_version"] = "1";
    expect_rejected(pack, "format_version");
}

TEST(RulesPack, RejectsMissingMetadata) {
    for (const char* key : {"id", "version", "date", "origin", "license"}) {
        json pack = minimal_pack();
        pack.erase(key);
        expect_rejected(pack, key);

        pack = minimal_pack();
        pack[key] = "";
        expect_rejected(pack, key);
    }
}

TEST(RulesPack, RejectsBadRules) {
    json pack = minimal_pack();
    pack.erase("rules");
    expect_rejected(pack, "rules");

    pack = minimal_pack();
    pack["rules"]["deck_max"] = 20;
    expect_rejected(pack, "deck_max");

    pack = minimal_pack();
    pack["rules"]["deck_min"] = 0;
    expect_rejected(pack, "deck_min");

    pack = minimal_pack();
    pack["rules"]["side_max"] = -1;
    expect_rejected(pack, "side_max");

    pack = minimal_pack();
    pack["rules"]["deck_min"] = 30.5;
    expect_rejected(pack, "deck_min");

    pack = minimal_pack();
    pack["rules"]["copy_limits"]["none"] = 1;
    expect_rejected(pack, "copy_limits.none");

    pack = minimal_pack();
    pack["rules"]["copy_limits"]["mythic"] = 1;
    expect_rejected(pack, "copy_limits.mythic");

    pack = minimal_pack();
    pack["rules"]["copy_limits"]["basic"] = 0;
    expect_rejected(pack, "copy_limits.basic");

    pack = minimal_pack();
    pack["rules"]["unaligned_cards_allowed"] = "yes";
    expect_rejected(pack, "unaligned_cards_allowed");
}

TEST(RulesPack, RejectsBadCards) {
    json pack = minimal_pack();
    pack["cards"] = json::object();
    expect_rejected(pack, "cards");

    pack = minimal_pack();
    pack["cards"].push_back(42);
    expect_rejected(pack, "cards[3]");

    pack = minimal_pack();
    pack["cards"].push_back(pack["cards"][2]);
    expect_rejected(pack, "duplicate");

    pack = minimal_pack();
    pack["cards"][2]["type"] = "creature";
    expect_rejected(pack, "cards[2].type");

    pack = minimal_pack();
    pack["cards"][2]["rank"] = "mythic";
    expect_rejected(pack, "cards[2].rank");

    pack = minimal_pack();
    pack["cards"][2]["rank"] = 1;
    expect_rejected(pack, "cards[2].rank");

    pack = minimal_pack();
    pack["cards"][2]["cost"] = -1;
    expect_rejected(pack, "cards[2].cost");

    pack = minimal_pack();
    pack["cards"][2]["cost"] = 18446744073709551615ULL;
    expect_rejected(pack, "cards[2].cost");

    pack = minimal_pack();
    pack["cards"][2]["sigils"] = "ash";
    expect_rejected(pack, "cards[2].sigils");

    pack = minimal_pack();
    pack["cards"][2]["sigils"] = json::array({""});
    expect_rejected(pack, "cards[2].sigils");

    pack = minimal_pack();
    pack["cards"][2]["id"] = std::string(65, 'X');
    expect_rejected(pack, "cards[2].id");

    pack = minimal_pack();
    pack["cards"][2].erase("name");
    expect_rejected(pack, "cards[2].name");
}

TEST(RulesPack, RejectsLegaciesWithoutTheirSeeker) {
    json pack = minimal_pack();
    pack["cards"][1].erase("seeker");
    expect_rejected(pack, "cards[1].seeker");

    pack = minimal_pack();
    pack["cards"][1].erase("starting_echo");
    expect_rejected(pack, "cards[1].starting_echo");

    pack = minimal_pack();
    pack["cards"][1]["seeker"] = "NOT-IN-PACK";
    expect_rejected(pack, "NOT-IN-PACK");

    pack = minimal_pack();
    pack["cards"][1]["seeker"] = "U";
    expect_rejected(pack, "Seeker that does not exist");
}

}  // namespace
