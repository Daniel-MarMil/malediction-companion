// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "mc/core/card.hpp"
#include "mc/core/deck.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mc::core {

/// Deck-building parameters. They come from the pack, not from the code (ADR-06),
/// so that rules revisions do not require publishing a new app.
struct DeckRules {
    int deck_min{30};
    int deck_max{50};
    int side_max{7};
    /// Maximum copies per rank. A rank without an entry cannot go in the deck: the
    /// legendary card is the Seeker itself.
    std::map<Rank, int> copy_limits{{Rank::Basic, 3}, {Rank::Elite, 2}, {Rank::Unique, 1}};
    /// Whether unaligned cards are allowed in any deck. The Seekers League builder allows
    /// them (checked on 2026-11-08).
    bool unaligned_cards_allowed{true};
    /// Whether side-deck copies count together with main-deck copies towards the limit
    /// (to be confirmed). If not, each deck meets the limit on its own.
    bool copy_limits_include_side{true};
};

/// Pack metadata (RF-02).
struct PackInfo {
    std::string id;
    std::string version;
    std::string date;
    std::string origin;
    std::string license;
};

/// A faction: the stable id that card sigils use, and the name to show (pack format 2).
struct Faction {
    std::string id;
    std::string name;
};

/// An official deck of a Seeker (RF-61), shipped inside the pack.
struct OfficialDeck {
    std::string id;
    std::string name;
    std::string url;  ///< Its Play Network page; empty if the pack does not give it.
    Deck deck;
};

/// Error while reading a rules pack.
class PackError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Immutable rules pack: metadata, deck rules and cards.
class RulesPack {
public:
    /// Reads a JSON pack. Throws PackError if it is not valid.
    [[nodiscard]] static RulesPack parse(std::string_view text);

    [[nodiscard]] const PackInfo& info() const noexcept { return info_; }
    [[nodiscard]] const DeckRules& deck_rules() const noexcept { return rules_; }
    [[nodiscard]] const std::vector<Faction>& factions() const noexcept { return factions_; }
    [[nodiscard]] const std::vector<OfficialDeck>& official_decks() const noexcept { return decks_; }
    [[nodiscard]] const std::vector<Card>& cards() const noexcept { return cards_; }
    [[nodiscard]] std::size_t card_count() const noexcept { return cards_.size(); }

    /// Looks up a card by its id; returns nullptr if there is none.
    [[nodiscard]] const Card* find(std::string_view id) const noexcept;

private:
    PackInfo info_;
    DeckRules rules_;
    std::vector<Faction> factions_;
    std::vector<Card> cards_;
    std::vector<OfficialDeck> decks_;
    std::map<std::string, std::size_t, std::less<>> index_;
};

}  // namespace mc::core
