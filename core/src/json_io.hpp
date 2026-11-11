// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0
//
// Conversion between JSON and the domain types. It is internal: JSON only crosses the
// boundary as bytes (ADR-11), so nlohmann::json does not appear in the public headers.

#pragma once

#include "mc/core/deck.hpp"
#include "mc/core/deck_text.hpp"
#include "mc/core/deck_validator.hpp"
#include "mc/core/rules_pack.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace mc::core::detail {

/// The received deck does not have the expected shape (this is not a rules problem).
class DeckFormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// A JSON integer that fits in an int; std::nullopt if it is not an integer or does not fit.
[[nodiscard]] std::optional<int> to_int(const nlohmann::json& value) noexcept;

/// Reads a deck shaped like {"seeker","legacy","terrain","main":[{"card","copies"}],"side":[...]}.
/// Throws DeckFormatError if the shape is not valid.
[[nodiscard]] Deck deck_from_json(const nlohmann::json& value);

/// The same shape that deck_from_json reads; a missing Seeker, Legacy or terrain is null.
[[nodiscard]] nlohmann::json deck_to_json(const Deck& deck);

/// Errors of an imported list: [{"line","error","detail"}].
[[nodiscard]] nlohmann::json deck_text_errors_to_json(const std::vector<DeckTextError>& errors);

[[nodiscard]] nlohmann::json issues_to_json(const std::vector<ValidationIssue>& issues);
[[nodiscard]] nlohmann::json pack_info_to_json(const RulesPack& pack);

/// Serializes to UTF-8; any invalid byte is replaced instead of throwing an exception.
[[nodiscard]] std::string to_text(const nlohmann::json& value);

}  // namespace mc::core::detail
