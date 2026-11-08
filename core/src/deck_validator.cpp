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

/// Only units, spells and items go in the deck. Seeker, Legacy and terrain have their own
/// slot, and relics form a separate shared deck.
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
    return text.empty() ? "none" : text;
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

    /// Each problem with a card is reported once, even if the card appears on several lines.
    void add_once(Rule rule, const std::string& card_id, std::string detail) {
        if (reported_.emplace(rule, card_id).second) {
            add(rule, card_id, std::move(detail));
        }
    }

    const Card* find_known(const std::string& id) {
        const Card* card = pack_.find(id);
        if (card == nullptr) {
            add_once(Rule::UnknownCard, id, "the card is not in the rules pack");
        }
        return card;
    }

    const Card* check_seeker() {
        if (deck_.seeker_id.empty()) {
            add(Rule::SeekerMissing, {}, "the deck needs a Seeker");
            return nullptr;
        }
        const Card* seeker = find_known(deck_.seeker_id);
        if (seeker != nullptr && seeker->type != CardType::Seeker) {
            add(Rule::SeekerWrongType, seeker->id, "it is a card of type " + std::string(to_string(seeker->type)));
            return nullptr;
        }
        return seeker;
    }

    void check_legacy(const Card* seeker) {
        if (deck_.legacy_id.empty()) {
            add(Rule::LegacyMissing, {}, "the deck needs the Legacy of its Seeker");
            return;
        }
        const Card* legacy = find_known(deck_.legacy_id);
        if (legacy == nullptr) {
            return;
        }
        if (legacy->type != CardType::Legacy) {
            add(Rule::LegacyWrongType, legacy->id, "it is a card of type " + std::string(to_string(legacy->type)));
        } else if (seeker != nullptr && legacy->seeker_id != seeker->id) {
            add(Rule::LegacyMismatch, legacy->id, "it belongs to " + legacy->seeker_id + ", not to " + seeker->id);
        }
    }

    // The terrain can belong to any faction, so its sigils are not compared.
    void check_terrain() {
        if (deck_.terrain_id.empty()) {
            add(Rule::TerrainMissing, {}, "the deck needs a terrain card");
            return;
        }
        const Card* terrain = find_known(deck_.terrain_id);
        if (terrain != nullptr && terrain->type != CardType::Terrain) {
            add(Rule::TerrainWrongType, terrain->id, "it is a card of type " + std::string(to_string(terrain->type)));
        }
    }

    void check_entries(const std::vector<DeckEntry>& entries, CopyCounts& copies, std::int64_t& count,
                       const Card* seeker) {
        for (const DeckEntry& entry : entries) {
            if (entry.copies < 1) {
                add_once(Rule::InvalidCopies, entry.card_id, "every line needs at least one copy");
                continue;
            }
            count += entry.copies;
            const Card* card = find_known(entry.card_id);
            if (card == nullptr) {
                continue;
            }
            if (!goes_in_deck(card->type)) {
                add_once(Rule::CardTypeNotAllowed, card->id,
                         "cards of type " + std::string(to_string(card->type)) + " do not go in the deck");
                continue;
            }
            if (!rules_.copy_limits.contains(card->rank)) {
                add_once(Rule::RankNotAllowed, card->id,
                         "cards of rank " + std::string(to_string(card->rank)) + " do not go in the deck");
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
                add_once(Rule::SigilMismatch, card.id, "these rules do not allow unaligned cards");
            }
            return;
        }
        if (seeker == nullptr) {
            return;  // Without a valid Seeker there are no sigils to compare with.
        }
        // A card is allowed only if every one of its sigils is on the Seeker.
        if (!std::ranges::includes(seeker->sigils, card.sigils)) {
            add_once(Rule::SigilMismatch, card.id,
                     "card sigils: " + join(card.sigils) + "; Seeker sigils: " + join(seeker->sigils));
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
            check_limits(main_copies_, " in the main deck");
            check_limits(side_copies_, " in the side deck");
        }
    }

    void check_limits(const CopyCounts& counts, std::string_view where) {
        for (const auto& [id, count] : counts) {
            const auto limit = rules_.copy_limits.find(count.rank);
            if (limit != rules_.copy_limits.end() && count.copies > limit->second) {
                add(Rule::TooManyCopies, id,
                    std::to_string(count.copies) + " copies" + std::string(where) + "; the maximum for rank " +
                        std::string(to_string(count.rank)) + " is " + std::to_string(limit->second));
            }
        }
    }

    void check_sizes() {
        if (main_count_ < rules_.deck_min) {
            add(Rule::DeckTooSmall, {},
                std::to_string(main_count_) + " cards; the minimum is " + std::to_string(rules_.deck_min));
        }
        if (main_count_ > rules_.deck_max) {
            add(Rule::DeckTooLarge, {},
                std::to_string(main_count_) + " cards; the maximum is " + std::to_string(rules_.deck_max));
        }
        if (side_count_ > rules_.side_max) {
            add(Rule::SideTooLarge, {},
                std::to_string(side_count_) + " cards in the side deck; the maximum is " +
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
