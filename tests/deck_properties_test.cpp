// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0
//
// Pruebas basadas en propiedades de la validación de mazos (RNF-17). RapidCheck genera
// cientos de mazos al azar y, si una propiedad falla, reduce el caso al mínimo.
// Para repetir un fallo: RC_PARAMS="seed=<semilla>" ctest -R DeckProperties

#include "mc/core/deck_validator.hpp"
#include "test_support.hpp"

#include <gtest/gtest.h>
#include <rapidcheck.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

namespace {

using namespace mc::core;
using mc::test::rule_set;
using mc::test::test_pack;

/// Copias máximas de una carta según su rango en el paquete de prueba.
int limit_of(const Card& card) {
    const auto& limits = test_pack().deck_rules().copy_limits;
    const auto limit = limits.find(card.rank);
    return limit == limits.end() ? 0 : limit->second;
}

/// Cartas que un mazo del Seeker de ceniza y marea puede llevar.
std::vector<const Card*> legal_for_dual_seeker() {
    const Card& seeker = *test_pack().find("T-SEEK-DUAL");
    std::vector<const Card*> result;
    for (const Card& card : test_pack().cards()) {
        const bool deck_type =
            card.type == CardType::Unit || card.type == CardType::Spell || card.type == CardType::Item;
        const bool fits =
            std::includes(seeker.sigils.begin(), seeker.sigils.end(), card.sigils.begin(), card.sigils.end());
        if (deck_type && limit_of(card) > 0 && fits) {
            result.push_back(&card);
        }
    }
    return result;
}

Deck dual_deck(std::vector<DeckEntry> main) {
    Deck deck;
    deck.seeker_id = "T-SEEK-DUAL";
    deck.legacy_id = "T-LEG-DUAL";
    deck.terrain_id = "T-TER-01";
    deck.main = std::move(main);
    return deck;
}

/// Baraja las líneas con claves generadas por RapidCheck, para que el caso sea reproducible.
std::vector<DeckEntry> shuffled(std::vector<DeckEntry> lines) {
    const auto keys =
        *rc::gen::container<std::vector<std::uint32_t>>(lines.size(), rc::gen::arbitrary<std::uint32_t>());
    std::vector<std::size_t> order(lines.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return keys[a] < keys[b]; });
    std::vector<DeckEntry> result;
    result.reserve(lines.size());
    for (const std::size_t index : order) {
        result.push_back(std::move(lines[index]));
    }
    return result;
}

std::int64_t total_copies(const std::vector<DeckEntry>& lines) {
    std::int64_t total = 0;
    for (const auto& line : lines) {
        total += line.copies;
    }
    return total;
}

TEST(DeckProperties, LegalCardsOnlyFailOnSize) {
    const auto legal = legal_for_dual_seeker();
    ASSERT_GE(legal.size(), 20U);
    const bool ok = rc::check("un mazo con cartas legales dentro de su límite solo falla por tamaño", [&] {
        std::vector<DeckEntry> lines;
        for (const Card* card : legal) {
            const int copies = *rc::gen::inRange(0, limit_of(*card) + 1);
            if (copies > 0) {
                lines.push_back({card->id, copies});
            }
        }
        const auto size = total_copies(lines);
        const auto issues = validate_deck(test_pack(), dual_deck(shuffled(std::move(lines))));
        for (const auto& issue : issues) {
            RC_ASSERT(issue.rule == Rule::DeckTooSmall || issue.rule == Rule::DeckTooLarge);
        }
        RC_ASSERT(issues.empty() == (size >= 30 && size <= 50));
    });
    EXPECT_TRUE(ok);
}

TEST(DeckProperties, OrderAndSplittingDoNotChangeTheResult) {
    std::vector<const Card*> all;
    for (const Card& card : test_pack().cards()) {
        all.push_back(&card);
    }
    const bool ok = rc::check("ni el orden ni partir las líneas cambian las incidencias", [&] {
        const auto picks = *rc::gen::container<std::vector<std::size_t>>(rc::gen::inRange<std::size_t>(0, all.size()));
        std::vector<DeckEntry> lines;
        std::vector<DeckEntry> split;
        for (const std::size_t pick : picks) {
            const int copies = *rc::gen::inRange(1, 5);
            lines.push_back({all[pick]->id, copies});
            for (int i = 0; i < copies; ++i) {
                split.push_back({all[pick]->id, 1});
            }
        }
        const auto original = validate_deck(test_pack(), dual_deck(lines));
        const auto rearranged = validate_deck(test_pack(), dual_deck(shuffled(std::move(split))));
        RC_ASSERT(rule_set(original) == rule_set(rearranged));
    });
    EXPECT_TRUE(ok);
}

TEST(DeckProperties, ExceedingALimitIsAlwaysReported) {
    const auto legal = legal_for_dual_seeker();
    const bool ok = rc::check("superar el límite de copias de una carta siempre se detecta", [&] {
        const Card* card = *rc::gen::elementOf(legal);
        const int copies = *rc::gen::inRange(limit_of(*card) + 1, 100);
        const bool in_side = *rc::gen::arbitrary<bool>();
        Deck deck = dual_deck({});
        auto& lines = in_side ? deck.side : deck.main;
        // Las copias pueden repartirse entre el mazo y el auxiliar: el límite es conjunto.
        const int first = *rc::gen::inRange(0, copies + 1);
        if (first > 0) {
            deck.main.push_back({card->id, first});
        }
        if (copies - first > 0) {
            lines.push_back({card->id, copies - first});
        }
        const auto issues = validate_deck(test_pack(), deck);
        RC_ASSERT(mc::test::has_issue(issues, Rule::TooManyCopies, card->id));
    });
    EXPECT_TRUE(ok);
}

TEST(DeckProperties, ValidationIsDeterministic) {
    std::vector<const Card*> all;
    for (const Card& card : test_pack().cards()) {
        all.push_back(&card);
    }
    const bool ok = rc::check("validar dos veces el mismo mazo da lo mismo", [&] {
        const auto picks = *rc::gen::container<std::vector<std::size_t>>(rc::gen::inRange<std::size_t>(0, all.size()));
        std::vector<DeckEntry> lines;
        for (const std::size_t pick : picks) {
            lines.push_back({all[pick]->id, *rc::gen::inRange(-1, 5)});
        }
        const Deck deck = dual_deck(std::move(lines));
        const auto first = validate_deck(test_pack(), deck);
        const auto second = validate_deck(test_pack(), deck);
        RC_ASSERT(first.size() == second.size());
        for (std::size_t i = 0; i < first.size(); ++i) {
            RC_ASSERT(first[i].rule == second[i].rule);
            RC_ASSERT(first[i].card_id == second[i].card_id);
            RC_ASSERT(first[i].detail == second[i].detail);
        }
    });
    EXPECT_TRUE(ok);
}

}  // namespace
