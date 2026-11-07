// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0
//
// Utilidades comunes de las pruebas. El paquete de prueba solo tiene cartas ficticias.

#pragma once

#include "mc/core/deck.hpp"
#include "mc/core/deck_validator.hpp"
#include "mc/core/rules_pack.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mc::test {

inline std::string read_file(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error(std::string("no se puede abrir ") + path);
    }
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

inline const std::string& test_pack_json() {
    static const std::string json = read_file(MC_TEST_PACK_PATH);
    return json;
}

inline const core::RulesPack& test_pack() {
    static const core::RulesPack pack = core::RulesPack::parse(test_pack_json());
    return pack;
}

inline std::vector<core::DeckEntry> entries(std::initializer_list<std::pair<const char*, int>> lines) {
    std::vector<core::DeckEntry> result;
    for (const auto& [card, copies] : lines) {
        result.push_back({card, copies});
    }
    return result;
}

/// Mazo válido de ceniza: 6 unidades, 3 hechizos y 1 objeto básicos, 3 copias de cada (30 cartas).
inline core::Deck valid_ash_deck() {
    core::Deck deck;
    deck.seeker_id = "T-SEEK-ASH";
    deck.legacy_id = "T-LEG-ASH";
    deck.terrain_id = "T-TER-01";
    deck.main = entries({{"T-ASH-U01", 3},
                         {"T-ASH-U02", 3},
                         {"T-ASH-U03", 3},
                         {"T-ASH-U04", 3},
                         {"T-ASH-U05", 3},
                         {"T-ASH-U06", 3},
                         {"T-ASH-S01", 3},
                         {"T-ASH-S02", 3},
                         {"T-ASH-S03", 3},
                         {"T-ASH-I01", 3}});
    return deck;
}

/// Pares (regla, carta) ordenados: sirven para comparar resultados sin depender del orden.
inline std::vector<std::pair<core::Rule, std::string>> rule_set(const std::vector<core::ValidationIssue>& issues) {
    std::vector<std::pair<core::Rule, std::string>> result;
    result.reserve(issues.size());
    for (const auto& issue : issues) {
        result.emplace_back(issue.rule, issue.card_id);
    }
    std::sort(result.begin(), result.end());
    return result;
}

inline std::size_t count_rule(const std::vector<core::ValidationIssue>& issues, core::Rule rule) {
    return static_cast<std::size_t>(
        std::count_if(issues.begin(), issues.end(), [rule](const auto& issue) { return issue.rule == rule; }));
}

inline bool has_issue(const std::vector<core::ValidationIssue>& issues, core::Rule rule,
                      const std::string& card_id = {}) {
    return std::any_of(issues.begin(), issues.end(), [&](const auto& issue) {
        return issue.rule == rule && (card_id.empty() || issue.card_id == card_id);
    });
}

}  // namespace mc::test
