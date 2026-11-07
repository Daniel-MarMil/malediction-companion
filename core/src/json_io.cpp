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

/// Límite defensivo de líneas por mazo; un mazo real tiene unas decenas.
constexpr std::size_t max_entries = 500;

std::string read_id(const json& deck, const char* key) {
    const auto field = deck.find(key);
    if (field == deck.end() || field->is_null()) {
        return {};
    }
    if (!field->is_string()) {
        throw DeckFormatError(std::string(key) + " debe ser un texto");
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
        throw DeckFormatError(std::string(key) + " debe ser una lista");
    }
    if (field->size() > max_entries) {
        throw DeckFormatError(std::string(key) + " tiene más de " + std::to_string(max_entries) + " líneas");
    }
    entries.reserve(field->size());
    for (const json& line : *field) {
        if (!line.is_object()) {
            throw DeckFormatError("cada línea de " + std::string(key) + " debe ser un objeto");
        }
        const auto card = line.find("card");
        if (card == line.end() || !card->is_string()) {
            throw DeckFormatError("cada línea de " + std::string(key) + " necesita el texto card");
        }
        DeckEntry entry;
        entry.card_id = card->get<std::string>();
        const auto copies = line.find("copies");
        if (copies != line.end()) {
            const auto value = to_int(*copies);
            if (!value) {
                throw DeckFormatError("copies debe ser un número entero");
            }
            entry.copies = *value;
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

}  // namespace

std::optional<int> to_int(const json& value) noexcept {
    // get_ptr no lanza excepciones: devuelve nullptr si el valor no es de ese tipo exacto.
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
        throw DeckFormatError("el mazo debe ser un objeto");
    }
    Deck deck;
    deck.seeker_id = read_id(value, "seeker");
    deck.legacy_id = read_id(value, "legacy");
    deck.terrain_id = read_id(value, "terrain");
    deck.main = read_entries(value, "main");
    deck.side = read_entries(value, "side");
    return deck;
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
