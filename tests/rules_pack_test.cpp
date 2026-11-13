// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#include "mc/core/rules_pack.hpp"
#include "test_support.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace mc::core;
using nlohmann::json;

/// A minimal valid pack; each test breaks it in one way.
json minimal_pack() {
    return json::parse(R"({
        "format": "mc-rules-pack", "format_version": 2,
        "id": "mini", "version": "1", "date": "2026-11-07", "origin": "tests", "license": "Apache-2.0",
        "rules": {"deck_min": 30, "deck_max": 50, "side_max": 7,
                  "copy_limits": {"basic": 3, "elite": 2, "unique": 1}},
        "factions": [{"id": "ash", "name": "Ash"}, {"id": "tide", "name": "Tide"}],
        "cards": [
            {"id": "S", "name": "Seeker", "type": "seeker", "rank": "legendary", "sigils": ["ash"]},
            {"id": "L", "name": "Legacy", "type": "legacy", "seeker": "S", "starting_echo": 4},
            {"id": "U", "name": "Unit", "type": "unit", "rank": "basic", "cost": 2, "sigils": ["ash"]},
            {"id": "T", "name": "Terrain", "type": "terrain"}
        ]
    })");
}

/// A minimal official deck for minimal_pack(); each deck test breaks it in one way.
json minimal_deck() {
    return json::parse(R"({
        "id": "D", "name": "Deck", "url": "https://example.invalid/d",
        "deck": {"seeker": "S", "legacy": "L", "terrain": "T", "main": [{"card": "U", "copies": 3}], "side": []}
    })");
}

json with_deck(const json& deck) {
    json pack = minimal_pack();
    pack["decks"] = json::array({deck});
    return pack;
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
    ASSERT_EQ(pack.factions().size(), 3U);
    EXPECT_EQ(pack.factions()[0].id, "ash");
    EXPECT_EQ(pack.factions()[0].name, "Ash Test Faction");
    ASSERT_EQ(pack.official_decks().size(), 1U);
    const OfficialDeck& deck = pack.official_decks()[0];
    EXPECT_EQ(deck.id, "test-ash-starter");
    EXPECT_EQ(deck.name, "Ash Test Starter");
    EXPECT_EQ(deck.url, "https://example.invalid/decks/ash-test-starter");
    EXPECT_EQ(deck.deck.seeker_id, "T-SEEK-ASH");
    EXPECT_EQ(deck.deck.main.size(), 10U);
    EXPECT_EQ(deck.deck.side.size(), 2U);

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

    const Card* full = pack.find("T-ASH-U01");
    ASSERT_NE(full, nullptr);
    EXPECT_EQ(full->traits, (std::vector<std::string>{"Test Trait", "Ember"}));
    EXPECT_EQ(full->abilities, (std::vector<std::string>{"Test Ability"}));
    ASSERT_TRUE(full->stats.has_value());
    EXPECT_EQ(
        *full->stats,
        (CardStats{
            .accuracy = 2, .power_hit = 3, .power_graze = 1, .range = 0, .speed = 4, .defense = 9, .max_health = 7}));
    EXPECT_FALSE(full->rules_text.has_value());
    ASSERT_TRUE(full->image.has_value());
    EXPECT_EQ(full->image->file, "images/T-ASH-U01.png");
    EXPECT_EQ(full->image->sha256, std::string(64, '0'));
    EXPECT_EQ(full->image->source, "https://example.invalid/T-ASH-U01.png");
    EXPECT_EQ(full->source, "https://example.invalid/card/ash-test-unit-1/");

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
    EXPECT_FALSE(relic->cost.has_value());
    EXPECT_TRUE(relic->sigils.empty());
    EXPECT_TRUE(relic->traits.empty());
    EXPECT_TRUE(relic->abilities.empty());
    EXPECT_FALSE(relic->stats.has_value());
    EXPECT_FALSE(relic->rules_text.has_value());
    EXPECT_FALSE(relic->image.has_value());
    EXPECT_TRUE(relic->source.empty());
    EXPECT_TRUE(parsed.official_decks().empty());
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

    // Format 1 is no longer read (2026-11-13), and a newer format is not read either.
    for (const int version : {1, 3}) {
        pack = minimal_pack();
        pack["format_version"] = version;
        expect_rejected(pack, "format_version");
    }

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
    expect_rejected(pack, "cards[4]");

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

    for (const char* id : {"with space", "acci\xC3\xB3n", "a/b"}) {
        pack = minimal_pack();
        pack["cards"][2]["id"] = id;
        expect_rejected(pack, "cards[2].id");
    }

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

TEST(RulesPack, ReadsNullOptionalFields) {
    json pack = minimal_pack();
    pack["cards"][2]["cost"] = nullptr;
    pack["cards"][2]["rules_text"] = "Deal 2 damage.";
    pack["cards"][2]["stats"] = nullptr;
    pack["cards"][2]["image"] = nullptr;
    pack["cards"][2]["source"] = nullptr;
    const RulesPack parsed = RulesPack::parse(pack.dump());
    const Card* unit = parsed.find("U");
    EXPECT_FALSE(unit->cost.has_value());
    EXPECT_EQ(unit->rules_text, "Deal 2 damage.");
    EXPECT_FALSE(unit->stats.has_value());
    EXPECT_FALSE(unit->image.has_value());
    EXPECT_TRUE(unit->source.empty());
}

TEST(RulesPack, RejectsBadFactions) {
    json pack = minimal_pack();
    pack.erase("factions");
    expect_rejected(pack, "factions");

    pack = minimal_pack();
    pack["factions"] = json::object();
    expect_rejected(pack, "factions");

    pack = minimal_pack();
    pack["factions"].push_back(42);
    expect_rejected(pack, "factions[2]");

    pack = minimal_pack();
    pack["factions"].push_back({{"id", "ash"}, {"name", "Again"}});
    expect_rejected(pack, "duplicate id 'ash'");

    pack = minimal_pack();
    pack["factions"][0]["id"] = "with space";
    expect_rejected(pack, "factions[0].id");

    pack = minimal_pack();
    pack["factions"][0].erase("name");
    expect_rejected(pack, "factions[0].name");

    pack = minimal_pack();
    pack["factions"] = json::array();
    for (int i = 0; i < 33; ++i) {
        pack["factions"].push_back({{"id", "f" + std::to_string(i)}, {"name", "F"}});
    }
    expect_rejected(pack, "at most 32");
}

TEST(RulesPack, RejectsSigilsOfUnknownFactions) {
    json pack = minimal_pack();
    pack["cards"][2]["sigils"] = json::array({"ash", "storm"});
    expect_rejected(pack, "cards[2].sigils: 'storm'");
}

TEST(RulesPack, RejectsBadCardDetails) {
    const std::vector<std::pair<std::pair<const char*, json>, std::string>> cases{
        {{"traits", "Ember"}, "cards[2].traits"},
        {{"traits", json::array({""})}, "cards[2].traits"},
        {{"abilities", json::array({1})}, "cards[2].abilities"},
        {{"abilities", json(std::vector<std::string>(33, "A"))}, "cards[2].abilities"},
        {{"cost", "2"}, "cards[2].cost"},
        {{"rules_text", ""}, "cards[2].rules_text"},
        {{"rules_text", 3}, "cards[2].rules_text"},
        {{"source", json::array()}, "cards[2].source"},
        {{"stats", 5}, "cards[2].stats"},
        {{"stats", json::object()}, "cards[2].stats.accuracy"},
        {{"image", "images/u.jpg"}, "cards[2].image"},
        {{"image", json::object()}, "cards[2].image.file"},
    };
    for (const auto& [field, fragment] : cases) {
        json pack = minimal_pack();
        pack["cards"][2][field.first] = field.second;
        expect_rejected(pack, fragment);
    }
}

TEST(RulesPack, RejectsBadStats) {
    const json stats = {{"accuracy", 2}, {"power_hit", 3}, {"power_graze", 1}, {"range", 0},
                        {"speed", 4},    {"defense", 9},   {"max_health", 7}};
    json pack = minimal_pack();
    pack["cards"][2]["stats"] = stats;
    EXPECT_NO_THROW((void)RulesPack::parse(pack.dump()));

    // Each stat may be null (the card's dash), which is not 0.
    for (const auto& [key, unused] : stats.items()) {
        pack["cards"][2]["stats"] = stats;
        pack["cards"][2]["stats"][key] = nullptr;
        const RulesPack parsed = RulesPack::parse(pack.dump());
        const CardStats& read = *parsed.find("U")->stats;
        const std::array<std::optional<int>, 7> values{read.accuracy, read.power_hit, read.power_graze, read.range,
                                                       read.speed,    read.defense,   read.max_health};
        EXPECT_EQ(std::ranges::count(values, std::optional<int>()), 1) << key;
        EXPECT_EQ(read.range, key == "range" ? std::nullopt : std::optional<int>(0)) << key;
    }

    for (const auto& [key, unused] : stats.items()) {
        pack["cards"][2]["stats"] = stats;
        pack["cards"][2]["stats"][key] = -1;
        expect_rejected(pack, "cards[2].stats." + key);

        pack["cards"][2]["stats"][key] = "3";
        expect_rejected(pack, "cards[2].stats." + key);

        pack["cards"][2]["stats"].erase(key);
        expect_rejected(pack, "cards[2].stats." + key);
    }

    pack["cards"][2]["stats"] = stats;
    pack["cards"][2]["stats"]["luck"] = 1;
    expect_rejected(pack, "other than the seven stats");
}

TEST(RulesPack, RejectsBadImages) {
    const json image = {{"file", "images/U.jpg"}, {"sha256", std::string(64, 'a')}, {"source", "https://x.invalid/u"}};
    json pack = minimal_pack();
    pack["cards"][2]["image"] = image;
    EXPECT_NO_THROW((void)RulesPack::parse(pack.dump()));

    for (const std::string& file :
         std::vector<std::string>{"", "/etc/passwd", "../U.jpg", "images/../../U.jpg", "images//U.jpg", "./U.jpg",
                                  "images/", "images\\U.jpg", "images/U jpg", std::string(201, 'a')}) {
        pack["cards"][2]["image"] = image;
        pack["cards"][2]["image"]["file"] = file;
        expect_rejected(pack, "cards[2].image.file");
    }
    for (const std::string& hash : std::vector<std::string>{std::string(63, 'a'), std::string(65, 'a'),
                                                            std::string(64, 'A'), std::string(63, 'a') + "g"}) {
        pack["cards"][2]["image"] = image;
        pack["cards"][2]["image"]["sha256"] = hash;
        expect_rejected(pack, "cards[2].image.sha256");
    }
    pack["cards"][2]["image"] = image;
    pack["cards"][2]["image"].erase("source");
    expect_rejected(pack, "cards[2].image.source");
}

TEST(RulesPack, ReadsOfficialDecks) {
    const RulesPack parsed = RulesPack::parse(with_deck(minimal_deck()).dump());
    ASSERT_EQ(parsed.official_decks().size(), 1U);
    const OfficialDeck& deck = parsed.official_decks()[0];
    EXPECT_EQ(deck.id, "D");
    EXPECT_EQ(deck.url, "https://example.invalid/d");
    EXPECT_EQ(deck.deck.terrain_id, "T");
    EXPECT_EQ(deck.deck.main, (std::vector<DeckEntry>{{"U", 3}}));

    json without_url = minimal_deck();
    without_url.erase("url");
    EXPECT_TRUE(RulesPack::parse(with_deck(without_url).dump()).official_decks()[0].url.empty());
}

TEST(RulesPack, RejectsBadOfficialDecks) {
    json pack = minimal_pack();
    pack["decks"] = json::object();
    expect_rejected(pack, "decks");

    expect_rejected(with_deck(42), "decks[0]");

    pack = with_deck(minimal_deck());
    pack["decks"].push_back(minimal_deck());
    expect_rejected(pack, "decks[1].id: duplicate");

    const std::vector<std::pair<std::pair<const char*, json>, std::string>> fields{
        {{"id", "bad id"}, "decks[0].id"},
        {{"name", ""}, "decks[0].name"},
        {{"url", 5}, "decks[0].url"},
        {{"deck", json::array()}, "decks[0].deck"},
    };
    for (const auto& [field, fragment] : fields) {
        json deck = minimal_deck();
        deck[field.first] = field.second;
        expect_rejected(with_deck(deck), fragment);
    }

    const std::vector<std::pair<std::pair<const char*, json>, std::string>> contents{
        {{"main", json::object()}, "decks[0].deck: main must be a list"},
        {{"seeker", "U"}, "decks[0].deck.seeker"},
        {{"seeker", nullptr}, "decks[0].deck.seeker"},
        {{"legacy", "S"}, "decks[0].deck.legacy"},
        {{"terrain", "NOT-IN-PACK"}, "decks[0].deck.terrain"},
        {{"main", json::array({{{"card", "NOT-IN-PACK"}}})}, "decks[0].deck.main[0]: unknown card"},
        {{"main", json::array({{{"card", "U"}, {"copies", 0}}})}, "decks[0].deck.main[0]: copies"},
        {{"side", json::array({{{"card", "U"}, {"copies", 100}}})}, "decks[0].deck.side[0]: copies"},
    };
    for (const auto& [field, fragment] : contents) {
        json deck = minimal_deck();
        deck["deck"][field.first] = field.second;
        expect_rejected(with_deck(deck), fragment);
    }

    // The Legacy must belong to the deck's Seeker.
    pack = minimal_pack();
    pack["cards"].push_back({{"id", "S2"}, {"name", "Other"}, {"type", "seeker"}, {"sigils", {"tide"}}});
    json deck = minimal_deck();
    deck["deck"]["seeker"] = "S2";
    pack["decks"] = json::array({deck});
    expect_rejected(pack, "not the Legacy of its Seeker");
}

}  // namespace
