// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0
//
// Deck lists as text, in the format malediction.gg exports (RF-09 and RF-10):
//
//   1x <Seeker>
//   1x <Legacy>
//   1x <terrain>
//
//   Main Deck
//   3x <card>
//   ...
//
//   Side Deck
//   2x <card>
//   ...
//
// Names may hold commas, and ’ and ' count as the same character. Any other section title
// opens an unknown section, and nothing under it is imported. Importing only reads the list;
// the deck rules are checked by validate_deck.

#pragma once

#include "mc/core/deck.hpp"
#include "mc/core/rules_pack.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mc::core {

/// Why a line of a deck list could not be imported. The text codes are stable.
enum class DeckTextIssue : std::uint8_t {
    UnrecognizedLine,  ///< unrecognized_line: like "Nx Name" but malformed, or a card name without copies.
    UnknownSection,    ///< unknown_section: a title other than Main Deck or Side Deck, and each line under it.
    BadCopies,         ///< bad_copies: copies outside 1 to 99, also once lines are merged; 1 for the first lines.
    UnknownCard,       ///< unknown_card: no card of the pack has that name.
    AmbiguousCard,     ///< ambiguous_card: several cards of the pack have that name.
    WrongSection,      ///< wrong_section: a card that does not go in the first lines, or a second Seeker.
    TooManyLines,      ///< too_many_lines: more than 500 different cards in a section.
};

[[nodiscard]] std::string_view to_string(DeckTextIssue issue) noexcept;

/// A line that could not be imported; line counts from 1.
struct DeckTextError {
    std::size_t line{0};
    DeckTextIssue issue{DeckTextIssue::UnrecognizedLine};
    std::string detail;
};

/// The deck read from the lines it understood, and an error for each line it did not.
struct ImportedDeck {
    Deck deck;
    std::vector<DeckTextError> errors;
    bool errors_truncated{false};  ///< More errors than max_deck_text_errors.
};

inline constexpr std::size_t max_deck_text_errors = 100;
inline constexpr int max_deck_text_copies = 99;

/// Reads a deck list with the card names of the pack. Several lines of the same card in a
/// section are merged into one, in the place of the first. A line that does not start with a
/// digit, is not Main Deck or Side Deck and is not the name of a card is an unknown section
/// title: it and every line under it, up to the next known title, get unknown_section and are
/// not imported. It never throws for bad text.
[[nodiscard]] ImportedDeck import_deck_text(const RulesPack& pack, std::string_view text);

/// A deck that cannot be written as a list that imports back to the same deck.
class DeckExportError : public std::runtime_error {
public:
    DeckExportError(std::string code, const std::string& detail) : std::runtime_error(detail), code_(std::move(code)) {}
    /// unknown_card, wrong_section, bad_copies or not_exportable.
    [[nodiscard]] const std::string& code() const noexcept { return code_; }

private:
    std::string code_;
};

/// Writes the deck as a list, with the names of the pack, merging repeated lines. Importing the
/// text gives the same deck with those lines merged; otherwise it throws DeckExportError.
[[nodiscard]] std::string export_deck_text(const RulesPack& pack, const Deck& deck);

}  // namespace mc::core
