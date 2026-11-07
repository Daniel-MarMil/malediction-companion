// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#include "mc/core/deck_validator.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace mc::core {
namespace {

constexpr std::array<std::pair<Rule, std::string_view>, 16> rule_names{{
    {Rule::SeekerMissing, "seeker_missing"},
    {Rule::SeekerWrongType, "seeker_wrong_type"},
    {Rule::LegacyMissing, "legacy_missing"},
    {Rule::LegacyWrongType, "legacy_wrong_type"},
    {Rule::LegacyMismatch, "legacy_mismatch"},
    {Rule::TerrainMissing, "terrain_missing"},
    {Rule::TerrainWrongType, "terrain_wrong_type"},
    {Rule::UnknownCard, "unknown_card"},
    {Rule::InvalidCopies, "invalid_copies"},
    {Rule::CardTypeNotAllowed, "card_type_not_allowed"},
    {Rule::RankNotAllowed, "rank_not_allowed"},
    {Rule::SigilMismatch, "sigil_mismatch"},
    {Rule::TooManyCopies, "too_many_copies"},
    {Rule::DeckTooSmall, "deck_too_small"},
    {Rule::DeckTooLarge, "deck_too_large"},
    {Rule::SideTooLarge, "side_too_large"},
}};

/// Solo unidades, hechizos y objetos van en el mazo. Seeker, Legacy y terreno tienen su
/// propio hueco, y las reliquias forman un mazo común aparte.
bool goes_in_deck(CardType type) noexcept {
    return type == CardType::Unit || type == CardType::Spell || type == CardType::Item;
}

std::string join(const std::vector<std::string>& items) {
    std::string text;
    for (const std::string& item : items) {
        if (!text.empty()) {
            text += ", ";
        }
        text += item;
    }
    return text.empty() ? "ninguno" : text;
}

struct CopyCount {
    Rank rank{Rank::None};
    std::int64_t copies{0};
};

using CopyCounts = std::map<std::string, CopyCount, std::less<>>;

class Checker {
public:
    Checker(const RulesPack& pack, const Deck& deck) : pack_(pack), rules_(pack.deck_rules()), deck_(deck) {}

    std::vector<ValidationIssue> run() {
        const Card* seeker = check_seeker();
        check_legacy(seeker);
        check_terrain();
        check_entries(deck_.main, main_copies_, main_count_, seeker);
        check_entries(deck_.side, side_copies_, side_count_, seeker);
        check_copy_limits();
        check_sizes();
        return std::move(issues_);
    }

private:
    void add(Rule rule, std::string card_id, std::string detail) {
        issues_.push_back({rule, std::move(card_id), std::move(detail)});
    }

    /// Cada problema de una carta se informa una sola vez, aunque aparezca en varias líneas.
    void add_once(Rule rule, const std::string& card_id, std::string detail) {
        if (reported_.emplace(rule, card_id).second) {
            add(rule, card_id, std::move(detail));
        }
    }

    const Card* find_known(const std::string& id) {
        const Card* card = pack_.find(id);
        if (card == nullptr) {
            add_once(Rule::UnknownCard, id, "la carta no está en el paquete de reglas");
        }
        return card;
    }

    const Card* check_seeker() {
        if (deck_.seeker_id.empty()) {
            add(Rule::SeekerMissing, {}, "el mazo necesita un Seeker");
            return nullptr;
        }
        const Card* seeker = find_known(deck_.seeker_id);
        if (seeker != nullptr && seeker->type != CardType::Seeker) {
            add(Rule::SeekerWrongType, seeker->id, "es una carta de tipo " + std::string(to_string(seeker->type)));
            return nullptr;
        }
        return seeker;
    }

    void check_legacy(const Card* seeker) {
        if (deck_.legacy_id.empty()) {
            add(Rule::LegacyMissing, {}, "el mazo necesita la Legacy de su Seeker");
            return;
        }
        const Card* legacy = find_known(deck_.legacy_id);
        if (legacy == nullptr) {
            return;
        }
        if (legacy->type != CardType::Legacy) {
            add(Rule::LegacyWrongType, legacy->id, "es una carta de tipo " + std::string(to_string(legacy->type)));
        } else if (seeker != nullptr && legacy->seeker_id != seeker->id) {
            add(Rule::LegacyMismatch, legacy->id, "pertenece a " + legacy->seeker_id + ", no a " + seeker->id);
        }
    }

    // El terreno puede ser de cualquier facción, así que no se comparan sus sigilos.
    void check_terrain() {
        if (deck_.terrain_id.empty()) {
            add(Rule::TerrainMissing, {}, "el mazo necesita una carta de terreno");
            return;
        }
        const Card* terrain = find_known(deck_.terrain_id);
        if (terrain != nullptr && terrain->type != CardType::Terrain) {
            add(Rule::TerrainWrongType, terrain->id, "es una carta de tipo " + std::string(to_string(terrain->type)));
        }
    }

    void check_entries(const std::vector<DeckEntry>& entries, CopyCounts& copies, std::int64_t& count,
                       const Card* seeker) {
        for (const DeckEntry& entry : entries) {
            if (entry.copies < 1) {
                add_once(Rule::InvalidCopies, entry.card_id, "cada línea necesita al menos una copia");
                continue;
            }
            count += entry.copies;
            const Card* card = find_known(entry.card_id);
            if (card == nullptr) {
                continue;
            }
            if (!goes_in_deck(card->type)) {
                add_once(Rule::CardTypeNotAllowed, card->id,
                         "las cartas de tipo " + std::string(to_string(card->type)) + " no van en el mazo");
                continue;
            }
            if (!rules_.copy_limits.contains(card->rank)) {
                add_once(Rule::RankNotAllowed, card->id,
                         "las cartas de rango " + std::string(to_string(card->rank)) + " no van en el mazo");
                continue;
            }
            CopyCount& total = copies[card->id];
            total.rank = card->rank;
            total.copies += entry.copies;
            check_sigils(*card, seeker);
        }
    }

    void check_sigils(const Card& card, const Card* seeker) {
        if (card.sigils.empty()) {
            if (!rules_.unaligned_cards_allowed) {
                add_once(Rule::SigilMismatch, card.id, "este reglamento no admite cartas sin facción");
            }
            return;
        }
        if (seeker == nullptr) {
            return;  // Sin un Seeker válido no hay sigilos con los que comparar.
        }
        // Una carta entra solo si todos sus sigilos están en el Seeker.
        if (!std::ranges::includes(seeker->sigils, card.sigils)) {
            add_once(Rule::SigilMismatch, card.id,
                     "sigilos de la carta: " + join(card.sigils) + "; del Seeker: " + join(seeker->sigils));
        }
    }

    void check_copy_limits() {
        if (rules_.copy_limits_include_side) {
            CopyCounts total = main_copies_;
            for (const auto& [id, side] : side_copies_) {
                CopyCount& count = total[id];
                count.rank = side.rank;
                count.copies += side.copies;
            }
            check_limits(total, "");
        } else {
            check_limits(main_copies_, " en el mazo principal");
            check_limits(side_copies_, " en el mazo auxiliar");
        }
    }

    void check_limits(const CopyCounts& counts, std::string_view where) {
        for (const auto& [id, count] : counts) {
            const auto limit = rules_.copy_limits.find(count.rank);
            if (limit != rules_.copy_limits.end() && count.copies > limit->second) {
                add(Rule::TooManyCopies, id,
                    std::to_string(count.copies) + " copias" + std::string(where) + "; el máximo para el rango " +
                        std::string(to_string(count.rank)) + " es " + std::to_string(limit->second));
            }
        }
    }

    void check_sizes() {
        if (main_count_ < rules_.deck_min) {
            add(Rule::DeckTooSmall, {},
                std::to_string(main_count_) + " cartas; el mínimo es " + std::to_string(rules_.deck_min));
        }
        if (main_count_ > rules_.deck_max) {
            add(Rule::DeckTooLarge, {},
                std::to_string(main_count_) + " cartas; el máximo es " + std::to_string(rules_.deck_max));
        }
        if (side_count_ > rules_.side_max) {
            add(Rule::SideTooLarge, {},
                std::to_string(side_count_) + " cartas en el mazo auxiliar; el máximo es " +
                    std::to_string(rules_.side_max));
        }
    }

    const RulesPack& pack_;
    const DeckRules& rules_;
    const Deck& deck_;
    std::vector<ValidationIssue> issues_;
    std::set<std::pair<Rule, std::string>, std::less<>> reported_;
    CopyCounts main_copies_;
    CopyCounts side_copies_;
    std::int64_t main_count_{0};
    std::int64_t side_count_{0};
};

}  // namespace

std::string_view to_string(Rule rule) noexcept {
    for (const auto& [value, name] : rule_names) {
        if (value == rule) {
            return name;
        }
    }
    return "unknown";
}

std::vector<ValidationIssue> validate_deck(const RulesPack& pack, const Deck& deck) {
    return Checker(pack, deck).run();
}

}  // namespace mc::core
