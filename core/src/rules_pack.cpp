// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#include "mc/core/rules_pack.hpp"

#include "json_io.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace mc::core {
namespace {

using nlohmann::json;

constexpr std::string_view pack_format = "mc-rules-pack";
constexpr int supported_format_version = 1;

// Defensive limits: a real pack has a few hundred cards.
constexpr std::size_t max_cards = 20000;
constexpr std::size_t max_id_length = 64;
constexpr std::size_t max_sigils = 8;
constexpr int max_number = 1000;

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
    card.id = require_string(value, "id", where);
    if (card.id.size() > max_id_length) {
        fail(where + ".id", "has more than " + std::to_string(max_id_length) + " characters");
    }
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
    card.cost = read_int(value, "cost", where, 0, max_number, 0);
    card.sigils = read_sigils(value, where);

    if (card.type == CardType::Legacy) {
        card.seeker_id = require_string(value, "seeker", where);
        card.starting_echo = read_int(value, "starting_echo", where, 0, max_number, std::nullopt);
    }
    return card;
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

    const json& cards = require_field(root, "cards", top);
    if (!cards.is_array() || cards.size() > max_cards) {
        fail("cards", "must be a list of at most " + std::to_string(max_cards) + " cards");
    }
    pack.cards_.reserve(cards.size());
    for (std::size_t i = 0; i < cards.size(); ++i) {
        const std::string where = "cards[" + std::to_string(i) + "]";
        Card card = read_card(cards[i], where);
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
    return pack;
}

const Card* RulesPack::find(std::string_view id) const noexcept {
    const auto entry = index_.find(id);
    return entry == index_.end() ? nullptr : &cards_[entry->second];
}

}  // namespace mc::core
