// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#include "json_io.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace mc::core::detail {
namespace {

using nlohmann::json;

/// Defensive limit on lines per deck; a real deck has a few dozen.
constexpr std::size_t max_entries = 500;

std::string read_id(const json& deck, const char* key) {
    const auto field = deck.find(key);
    if (field == deck.end() || field->is_null()) {
        return {};
    }
    if (!field->is_string()) {
        throw DeckFormatError(std::string(key) + " must be a string");
    }
    return field->get<std::string>();
}

std::vector<DeckEntry> read_entries(const json& deck, const char* key) {
    std::vector<DeckEntry> entries;
    const auto field = deck.find(key);
    if (field == deck.end() || field->is_null()) {
        return entries;
    }
    if (!field->is_array()) {
        throw DeckFormatError(std::string(key) + " must be a list");
    }
    if (field->size() > max_entries) {
        throw DeckFormatError(std::string(key) + " has more than " + std::to_string(max_entries) + " lines");
    }
    entries.reserve(field->size());
    for (const json& line : *field) {
        if (!line.is_object()) {
            throw DeckFormatError("every line of " + std::string(key) + " must be an object");
        }
        const auto card = line.find("card");
        if (card == line.end() || !card->is_string()) {
            throw DeckFormatError("every line of " + std::string(key) + " needs a card string");
        }
        DeckEntry entry;
        entry.card_id = card->get<std::string>();
        const auto copies = line.find("copies");
        if (copies != line.end()) {
            const auto value = to_int(*copies);
            if (!value) {
                throw DeckFormatError("copies must be an integer");
            }
            entry.copies = *value;
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

}  // namespace

std::optional<int> to_int(const json& value) noexcept {
    // get_ptr does not throw: it returns nullptr if the value is not of that exact type.
    if (const auto* number = value.get_ptr<const json::number_unsigned_t*>(); number != nullptr) {
        if (*number > static_cast<json::number_unsigned_t>(std::numeric_limits<int>::max())) {
            return std::nullopt;
        }
        return static_cast<int>(*number);
    }
    if (const auto* number = value.get_ptr<const json::number_integer_t*>(); number != nullptr) {
        if (*number < std::numeric_limits<int>::min() || *number > std::numeric_limits<int>::max()) {
            return std::nullopt;
        }
        return static_cast<int>(*number);
    }
    return std::nullopt;
}

Deck deck_from_json(const json& value) {
    if (!value.is_object()) {
        throw DeckFormatError("the deck must be an object");
    }
    Deck deck;
    deck.seeker_id = read_id(value, "seeker");
    deck.legacy_id = read_id(value, "legacy");
    deck.terrain_id = read_id(value, "terrain");
    deck.main = read_entries(value, "main");
    deck.side = read_entries(value, "side");
    return deck;
}

json deck_to_json(const Deck& deck) {
    const auto id = [](const std::string& value) { return value.empty() ? json(nullptr) : json(value); };
    const auto entries = [](const std::vector<DeckEntry>& lines) {
        json result = json::array();
        for (const DeckEntry& entry : lines) {
            json item = json::object();
            item["card"] = entry.card_id;
            item["copies"] = entry.copies;
            result.push_back(std::move(item));
        }
        return result;
    };
    json result = json::object();
    result["seeker"] = id(deck.seeker_id);
    result["legacy"] = id(deck.legacy_id);
    result["terrain"] = id(deck.terrain_id);
    result["main"] = entries(deck.main);
    result["side"] = entries(deck.side);
    return result;
}

json deck_text_errors_to_json(const std::vector<DeckTextError>& errors) {
    json result = json::array();
    for (const DeckTextError& error : errors) {
        json item = json::object();
        item["line"] = error.line;
        item["error"] = std::string(to_string(error.issue));
        item["detail"] = error.detail;
        result.push_back(std::move(item));
    }
    return result;
}

json issues_to_json(const std::vector<ValidationIssue>& issues) {
    json result = json::array();
    for (const ValidationIssue& issue : issues) {
        json item = json::object();
        item["rule"] = std::string(to_string(issue.rule));
        if (!issue.card_id.empty()) {
            item["card"] = issue.card_id;
        }
        item["detail"] = issue.detail;
        result.push_back(std::move(item));
    }
    return result;
}

json pack_info_to_json(const RulesPack& pack) {
    const PackInfo& info = pack.info();
    json result = json::object();
    result["id"] = info.id;
    result["version"] = info.version;
    result["date"] = info.date;
    result["origin"] = info.origin;
    result["license"] = info.license;
    result["cards"] = pack.card_count();
    return result;
}

std::string to_text(const json& value) {
    return value.dump(-1, ' ', false, json::error_handler_t::replace);
}

}  // namespace mc::core::detail
