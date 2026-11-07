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

// Límites defensivos: un paquete real tiene unos cientos de cartas.
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
        fail(path_of(where, key), "falta el campo");
    }
    return *value;
}

const json& require_object(const json& object, const char* key, const std::string& where) {
    const json& value = require_field(object, key, where);
    if (!value.is_object()) {
        fail(path_of(where, key), "debe ser un objeto");
    }
    return value;
}

std::string require_string(const json& object, const char* key, const std::string& where) {
    const json& value = require_field(object, key, where);
    if (!value.is_string() || value.get_ref<const std::string&>().empty()) {
        fail(path_of(where, key), "debe ser un texto no vacío");
    }
    return value.get<std::string>();
}

int to_bounded_int(const json& value, const std::string& path, int min, int max) {
    const auto number = detail::to_int(value);
    if (!number || *number < min || *number > max) {
        fail(path, "debe ser un entero entre " + std::to_string(min) + " y " + std::to_string(max));
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
        fail(path_of(where, key), "falta el campo");
    }
    return to_bounded_int(*value, path_of(where, key), min, max);
}

bool read_bool(const json& object, const char* key, const std::string& where, bool fallback) {
    const json* value = find_field(object, key);
    if (value == nullptr) {
        return fallback;
    }
    if (!value->is_boolean()) {
        fail(path_of(where, key), "debe ser true o false");
    }
    return value->get<bool>();
}

DeckRules read_rules(const json& object) {
    const std::string where = "rules";
    DeckRules rules;
    rules.deck_min = read_int(object, "deck_min", where, 1, max_number, std::nullopt);
    rules.deck_max = read_int(object, "deck_max", where, 1, max_number, std::nullopt);
    if (rules.deck_max < rules.deck_min) {
        fail(where + ".deck_max", "no puede ser menor que deck_min");
    }
    rules.side_max = read_int(object, "side_max", where, 0, max_number, std::nullopt);

    rules.copy_limits.clear();
    const json& limits = require_object(object, "copy_limits", where);
    for (auto item = limits.begin(); item != limits.end(); ++item) {
        const std::string path = where + ".copy_limits." + item.key();
        const auto rank = parse_rank(item.key());
        if (!rank || *rank == Rank::None) {
            fail(path, "rango desconocido");
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
        fail(path, "debe ser una lista de " + std::to_string(max_sigils) + " sigilos como máximo");
    }
    for (const json& sigil : *value) {
        if (!sigil.is_string() || sigil.get_ref<const std::string&>().empty()) {
            fail(path, "cada sigilo debe ser un texto no vacío");
        }
        sigils.push_back(sigil.get<std::string>());
    }
    // Ordenados y sin repetir, para comparar conjuntos con std::includes.
    std::sort(sigils.begin(), sigils.end());
    sigils.erase(std::unique(sigils.begin(), sigils.end()), sigils.end());
    return sigils;
}

Card read_card(const json& value, const std::string& where) {
    if (!value.is_object()) {
        fail(where, "debe ser un objeto");
    }
    Card card;
    card.id = require_string(value, "id", where);
    if (card.id.size() > max_id_length) {
        fail(where + ".id", "tiene más de " + std::to_string(max_id_length) + " caracteres");
    }
    card.name = require_string(value, "name", where);

    const std::string type_name = require_string(value, "type", where);
    const auto type = parse_card_type(type_name);
    if (!type) {
        fail(where + ".type", "tipo desconocido '" + type_name + "'");
    }
    card.type = *type;

    if (const json* rank_value = find_field(value, "rank"); rank_value != nullptr) {
        const auto rank =
            rank_value->is_string() ? parse_rank(rank_value->get_ref<const std::string&>()) : std::nullopt;
        if (!rank) {
            fail(where + ".rank", "rango desconocido");
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
        throw PackError(std::string("JSON no válido: ") + error.what());
    }
    if (!root.is_object()) {
        throw PackError("el paquete debe ser un objeto JSON");
    }

    const std::string format = require_string(root, "format", "");
    if (format != pack_format) {
        fail("format", "se esperaba '" + std::string(pack_format) + "'");
    }
    const int version = read_int(root, "format_version", "", 1, max_number, std::nullopt);
    if (version != supported_format_version) {
        fail("format_version", "la versión " + std::to_string(version) + " no está admitida");
    }

    // Ruta vacía para los campos de primer nivel. Es una variable con nombre para que las
    // referencias devueltas por require_* no parezcan ligadas a un temporal.
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
        fail("cards", "debe ser una lista de " + std::to_string(max_cards) + " cartas como máximo");
    }
    pack.cards_.reserve(cards.size());
    for (std::size_t i = 0; i < cards.size(); ++i) {
        const std::string where = "cards[" + std::to_string(i) + "]";
        Card card = read_card(cards[i], where);
        if (!pack.index_.emplace(card.id, pack.cards_.size()).second) {
            fail(where + ".id", "identificador repetido '" + card.id + "'");
        }
        pack.cards_.push_back(std::move(card));
    }

    // Cada Legacy debe pertenecer a un Seeker del mismo paquete.
    for (const Card& card : pack.cards_) {
        if (card.type != CardType::Legacy) {
            continue;
        }
        const Card* seeker = pack.find(card.seeker_id);
        if (seeker == nullptr || seeker->type != CardType::Seeker) {
            fail("cards", "la Legacy '" + card.id + "' apunta a un Seeker que no existe: '" + card.seeker_id + "'");
        }
    }
    return pack;
}

const Card* RulesPack::find(std::string_view id) const noexcept {
    const auto entry = index_.find(id);
    return entry == index_.end() ? nullptr : &cards_[entry->second];
}

}  // namespace mc::core
