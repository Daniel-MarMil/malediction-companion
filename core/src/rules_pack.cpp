// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#include "mc/core/rules_pack.hpp"

#include "json_io.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace mc::core {
namespace {

using nlohmann::json;

constexpr std::string_view pack_format = "mc-rules-pack";
// Format 2 (2026-11-13) adds factions, traits, abilities, stats, images and official decks.
// Format 1 is no longer read: no published pack or app used it.
constexpr int supported_format_version = 2;

// Defensive limits: a real pack has a few hundred cards.
constexpr std::size_t max_cards = 20000;
constexpr std::size_t max_id_length = 64;
constexpr std::size_t max_sigils = 8;
constexpr std::size_t max_factions = 32;
constexpr std::size_t max_labels = 32;
constexpr std::size_t max_decks = 1000;
constexpr std::size_t max_path_length = 200;
constexpr std::size_t sha256_hex_length = 64;
constexpr int max_number = 1000;
constexpr int max_deck_copies = 99;

[[noreturn]] void fail(const std::string& where, const std::string& what) {
    throw PackError(where + ": " + what);
}

std::string path_of(const std::string& where, const char* key) {
    return where.empty() ? std::string(key) : where + "." + key;
}

const json* find_field(const json& object, const char* key) {
    const auto field = object.find(key);
    return field == object.end() ? nullptr : &*field;
}

const json& require_field(const json& object, const char* key, const std::string& where) {
    const json* value = find_field(object, key);
    if (value == nullptr) {
        fail(path_of(where, key), "the field is missing");
    }
    return *value;
}

const json& require_object(const json& object, const char* key, const std::string& where) {
    const json& value = require_field(object, key, where);
    if (!value.is_object()) {
        fail(path_of(where, key), "must be an object");
    }
    return value;
}

std::string require_string(const json& object, const char* key, const std::string& where) {
    const json& value = require_field(object, key, where);
    if (!value.is_string() || value.get_ref<const std::string&>().empty()) {
        fail(path_of(where, key), "must be a non-empty string");
    }
    return value.get<std::string>();
}

int to_bounded_int(const json& value, const std::string& path, int min, int max) {
    const auto number = detail::to_int(value);
    if (!number || *number < min || *number > max) {
        fail(path, "must be an integer between " + std::to_string(min) + " and " + std::to_string(max));
    }
    return *number;
}

int read_int(const json& object, const char* key, const std::string& where, int min, int max,
             std::optional<int> fallback) {
    const json* value = find_field(object, key);
    if (value == nullptr) {
        if (fallback) {
            return *fallback;
        }
        fail(path_of(where, key), "the field is missing");
    }
    return to_bounded_int(*value, path_of(where, key), min, max);
}

bool read_bool(const json& object, const char* key, const std::string& where, bool fallback) {
    const json* value = find_field(object, key);
    if (value == nullptr) {
        return fallback;
    }
    if (!value->is_boolean()) {
        fail(path_of(where, key), "must be true or false");
    }
    return value->get<bool>();
}

bool is_id_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
           c == '.';
}

/// An id of a card, faction or deck: 1 to 64 ASCII letters, digits, '-', '_' or '.'.
std::string require_id(const json& object, const char* key, const std::string& where) {
    std::string id = require_string(object, key, where);
    if (id.size() > max_id_length) {
        fail(path_of(where, key), "has more than " + std::to_string(max_id_length) + " characters");
    }
    if (!std::ranges::all_of(id, is_id_char)) {
        fail(path_of(where, key), "may only hold ASCII letters, digits, '-', '_' and '.'");
    }
    return id;
}

/// An optional string: a missing field or null gives std::nullopt.
std::optional<std::string> optional_string(const json& object, const char* key, const std::string& where) {
    const json* value = find_field(object, key);
    if (value == nullptr || value->is_null()) {
        return std::nullopt;
    }
    if (!value->is_string() || value->get_ref<const std::string&>().empty()) {
        fail(path_of(where, key), "must be a non-empty string or null");
    }
    return value->get<std::string>();
}

/// An optional list of non-empty strings, kept in its order.
std::vector<std::string> read_labels(const json& object, const char* key, const std::string& where) {
    std::vector<std::string> labels;
    const json* value = find_field(object, key);
    if (value == nullptr) {
        return labels;
    }
    const std::string path = path_of(where, key);
    if (!value->is_array() || value->size() > max_labels) {
        fail(path, "must be a list of at most " + std::to_string(max_labels) + " strings");
    }
    for (const json& label : *value) {
        if (!label.is_string() || label.get_ref<const std::string&>().empty()) {
            fail(path, "every entry must be a non-empty string");
        }
        labels.push_back(label.get<std::string>());
    }
    return labels;
}

std::optional<int> read_cost(const json& card, const std::string& where) {
    const json* value = find_field(card, "cost");
    if (value == nullptr || value->is_null()) {
        return std::nullopt;
    }
    return to_bounded_int(*value, where + ".cost", 0, max_number);
}

std::optional<CardStats> read_stats(const json& card, const std::string& where) {
    const json* value = find_field(card, "stats");
    if (value == nullptr || value->is_null()) {
        return std::nullopt;
    }
    const std::string path = where + ".stats";
    if (!value->is_object()) {
        fail(path, "must be an object");
    }
    CardStats stats;
    const std::array<std::pair<const char*, std::optional<int>*>, 7> fields{{
        {"accuracy", &stats.accuracy},
        {"power_hit", &stats.power_hit},
        {"power_graze", &stats.power_graze},
        {"range", &stats.range},
        {"speed", &stats.speed},
        {"defense", &stats.defense},
        {"max_health", &stats.max_health},
    }};
    // Every stat is required: a number, or null for the card's dash (a null stat, never 0).
    for (const auto& [key, target] : fields) {
        const json& stat = require_field(*value, key, path);
        if (!stat.is_null()) {
            *target = to_bounded_int(stat, path_of(path, key), 0, max_number);
        }
    }
    if (value->size() != fields.size()) {
        fail(path, "has fields other than the seven stats");
    }
    return stats;
}

/// A relative path inside the pack folder: no leading '/', no '.' or '..' segments.
bool is_safe_relative_path(const std::string& path) {
    if (path.empty() || path.size() > max_path_length) {
        return false;
    }
    const bool allowed_chars = std::ranges::all_of(path, [](char c) { return is_id_char(c) || c == '/'; });
    if (!allowed_chars) {
        return false;
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = std::min(path.find('/', start), path.size());
        const std::string_view segment(path.data() + start, end - start);
        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }
        start = end + 1;
    }
    return true;
}

bool is_sha256_hex(const std::string& text) {
    return text.size() == sha256_hex_length &&
           std::ranges::all_of(text, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

std::optional<CardImage> read_image(const json& card, const std::string& where) {
    const json* value = find_field(card, "image");
    if (value == nullptr || value->is_null()) {
        return std::nullopt;
    }
    const std::string path = where + ".image";
    if (!value->is_object()) {
        fail(path, "must be an object");
    }
    CardImage image;
    image.file = require_string(*value, "file", path);
    if (!is_safe_relative_path(image.file)) {
        fail(path + ".file", "must be a relative path inside the pack folder");
    }
    image.sha256 = require_string(*value, "sha256", path);
    if (!is_sha256_hex(image.sha256)) {
        fail(path + ".sha256", "must be 64 lowercase hexadecimal digits");
    }
    image.source = require_string(*value, "source", path);
    return image;
}

std::vector<Faction> read_factions(const json& root) {
    const std::string top;
    const json& value = require_field(root, "factions", top);
    if (!value.is_array() || value.size() > max_factions) {
        fail("factions", "must be a list of at most " + std::to_string(max_factions) + " factions");
    }
    std::vector<Faction> factions;
    std::set<std::string, std::less<>> seen;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const std::string where = "factions[" + std::to_string(i) + "]";
        if (!value[i].is_object()) {
            fail(where, "must be an object");
        }
        Faction faction{.id = require_id(value[i], "id", where), .name = require_string(value[i], "name", where)};
        if (!seen.insert(faction.id).second) {
            fail(where + ".id", "duplicate id '" + faction.id + "'");
        }
        factions.push_back(std::move(faction));
    }
    return factions;
}

DeckRules read_rules(const json& object) {
    const std::string where = "rules";
    DeckRules rules;
    rules.deck_min = read_int(object, "deck_min", where, 1, max_number, std::nullopt);
    rules.deck_max = read_int(object, "deck_max", where, 1, max_number, std::nullopt);
    if (rules.deck_max < rules.deck_min) {
        fail(where + ".deck_max", "cannot be less than deck_min");
    }
    rules.side_max = read_int(object, "side_max", where, 0, max_number, std::nullopt);

    rules.copy_limits.clear();
    const json& limits = require_object(object, "copy_limits", where);
    for (auto item = limits.begin(); item != limits.end(); ++item) {
        const std::string path = where + ".copy_limits." + item.key();
        const auto rank = parse_rank(item.key());
        if (!rank || *rank == Rank::None) {
            fail(path, "unknown rank");
        }
        rules.copy_limits[*rank] = to_bounded_int(item.value(), path, 1, max_number);
    }
    rules.unaligned_cards_allowed = read_bool(object, "unaligned_cards_allowed", where, true);
    rules.copy_limits_include_side = read_bool(object, "copy_limits_include_side", where, true);
    return rules;
}

std::vector<std::string> read_sigils(const json& card, const std::string& where) {
    std::vector<std::string> sigils;
    const json* value = find_field(card, "sigils");
    if (value == nullptr) {
        return sigils;
    }
    const std::string path = where + ".sigils";
    if (!value->is_array() || value->size() > max_sigils) {
        fail(path, "must be a list of at most " + std::to_string(max_sigils) + " sigils");
    }
    for (const json& sigil : *value) {
        if (!sigil.is_string() || sigil.get_ref<const std::string&>().empty()) {
            fail(path, "every sigil must be a non-empty string");
        }
        sigils.push_back(sigil.get<std::string>());
    }
    // Sorted and without duplicates, so that sets can be compared with std::ranges::includes.
    std::ranges::sort(sigils);
    const auto repeated = std::ranges::unique(sigils);
    sigils.erase(repeated.begin(), repeated.end());
    return sigils;
}

Card read_card(const json& value, const std::string& where) {
    if (!value.is_object()) {
        fail(where, "must be an object");
    }
    Card card;
    card.id = require_id(value, "id", where);
    card.name = require_string(value, "name", where);

    const std::string type_name = require_string(value, "type", where);
    const auto type = parse_card_type(type_name);
    if (!type) {
        fail(where + ".type", "unknown type '" + type_name + "'");
    }
    card.type = *type;

    if (const json* rank_value = find_field(value, "rank"); rank_value != nullptr) {
        const auto rank =
            rank_value->is_string() ? parse_rank(rank_value->get_ref<const std::string&>()) : std::nullopt;
        if (!rank) {
            fail(where + ".rank", "unknown rank");
        }
        card.rank = *rank;
    }
    card.cost = read_cost(value, where);
    card.sigils = read_sigils(value, where);
    card.traits = read_labels(value, "traits", where);
    card.abilities = read_labels(value, "abilities", where);
    card.stats = read_stats(value, where);
    card.rules_text = optional_string(value, "rules_text", where);
    card.image = read_image(value, where);
    card.source = optional_string(value, "source", where).value_or("");

    if (card.type == CardType::Legacy) {
        card.seeker_id = require_string(value, "seeker", where);
        card.starting_echo = read_int(value, "starting_echo", where, 0, max_number, std::nullopt);
    }
    return card;
}

/// The id of a deck's Seeker, Legacy or terrain: it must be a card of that type.
void check_deck_card(const RulesPack& pack, const std::string& id, CardType type, const std::string& path) {
    const Card* card = pack.find(id);
    if (card == nullptr || card->type != type) {
        fail(path, "'" + id + "' is not a " + std::string(to_string(type)) + " of the pack");
    }
}

void check_deck_entries(const RulesPack& pack, const std::vector<DeckEntry>& entries, const std::string& path) {
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const std::string where = path + "[" + std::to_string(i) + "]";
        if (pack.find(entries[i].card_id) == nullptr) {
            fail(where, "unknown card '" + entries[i].card_id + "'");
        }
        if (entries[i].copies < 1 || entries[i].copies > max_deck_copies) {
            fail(where, "copies must be between 1 and " + std::to_string(max_deck_copies));
        }
    }
}

/// Official decks (RF-61). Their cards must be in the pack; the deck rules are not checked
/// here, because a later rules revision may leave an official deck invalid (RF-07).
std::vector<OfficialDeck> read_decks(const json& root, const RulesPack& pack) {
    std::vector<OfficialDeck> decks;
    const json* value = find_field(root, "decks");
    if (value == nullptr) {
        return decks;
    }
    if (!value->is_array() || value->size() > max_decks) {
        fail("decks", "must be a list of at most " + std::to_string(max_decks) + " decks");
    }
    std::set<std::string, std::less<>> seen;
    for (std::size_t i = 0; i < value->size(); ++i) {
        const std::string where = "decks[" + std::to_string(i) + "]";
        const json& item = (*value)[i];
        if (!item.is_object()) {
            fail(where, "must be an object");
        }
        OfficialDeck deck;
        deck.id = require_id(item, "id", where);
        if (!seen.insert(deck.id).second) {
            fail(where + ".id", "duplicate id '" + deck.id + "'");
        }
        deck.name = require_string(item, "name", where);
        deck.url = optional_string(item, "url", where).value_or("");
        try {
            deck.deck = detail::deck_from_json(require_object(item, "deck", where));
        } catch (const detail::DeckFormatError& error) {
            fail(where + ".deck", error.what());
        }
        check_deck_card(pack, deck.deck.seeker_id, CardType::Seeker, where + ".deck.seeker");
        check_deck_card(pack, deck.deck.legacy_id, CardType::Legacy, where + ".deck.legacy");
        check_deck_card(pack, deck.deck.terrain_id, CardType::Terrain, where + ".deck.terrain");
        if (pack.find(deck.deck.legacy_id)->seeker_id != deck.deck.seeker_id) {
            fail(where + ".deck.legacy", "'" + deck.deck.legacy_id + "' is not the Legacy of its Seeker");
        }
        check_deck_entries(pack, deck.deck.main, where + ".deck.main");
        check_deck_entries(pack, deck.deck.side, where + ".deck.side");
        decks.push_back(std::move(deck));
    }
    return decks;
}

}  // namespace

RulesPack RulesPack::parse(std::string_view text) {
    json root;
    try {
        root = json::parse(text.begin(), text.end());
    } catch (const json::exception& error) {
        throw PackError(std::string("invalid JSON: ") + error.what());
    }
    if (!root.is_object()) {
        throw PackError("the pack must be a JSON object");
    }

    const std::string format = require_string(root, "format", "");
    if (format != pack_format) {
        fail("format", "expected '" + std::string(pack_format) + "'");
    }
    const int version = read_int(root, "format_version", "", 1, max_number, std::nullopt);
    if (version != supported_format_version) {
        fail("format_version", "version " + std::to_string(version) + " is not supported");
    }

    // Empty path for top-level fields. It is a named variable so that the references
    // returned by require_* do not look bound to a temporary.
    const std::string top;
    RulesPack pack;
    pack.info_.id = require_string(root, "id", top);
    pack.info_.version = require_string(root, "version", top);
    pack.info_.date = require_string(root, "date", top);
    pack.info_.origin = require_string(root, "origin", top);
    pack.info_.license = require_string(root, "license", top);
    pack.rules_ = read_rules(require_object(root, "rules", top));
    pack.factions_ = read_factions(root);

    const json& cards = require_field(root, "cards", top);
    if (!cards.is_array() || cards.size() > max_cards) {
        fail("cards", "must be a list of at most " + std::to_string(max_cards) + " cards");
    }
    pack.cards_.reserve(cards.size());
    for (std::size_t i = 0; i < cards.size(); ++i) {
        const std::string where = "cards[" + std::to_string(i) + "]";
        Card card = read_card(cards[i], where);
        for (const std::string& sigil : card.sigils) {
            const bool known = std::ranges::any_of(pack.factions_, [&](const Faction& f) { return f.id == sigil; });
            if (!known) {
                fail(where + ".sigils", "'" + sigil + "' is not a faction of the pack");
            }
        }
        if (!pack.index_.emplace(card.id, pack.cards_.size()).second) {
            fail(where + ".id", "duplicate id '" + card.id + "'");
        }
        pack.cards_.push_back(std::move(card));
    }

    // Every Legacy must belong to a Seeker in the same pack.
    for (const Card& card : pack.cards_) {
        if (card.type != CardType::Legacy) {
            continue;
        }
        const Card* seeker = pack.find(card.seeker_id);
        if (seeker == nullptr || seeker->type != CardType::Seeker) {
            fail("cards",
                 "the Legacy '" + card.id + "' points to a Seeker that does not exist: '" + card.seeker_id + "'");
        }
    }
    pack.decks_ = read_decks(root, pack);
    return pack;
}

const Card* RulesPack::find(std::string_view id) const noexcept {
    const auto entry = index_.find(id);
    return entry == index_.end() ? nullptr : &cards_[entry->second];
}

}  // namespace mc::core
