// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0
//
// Conversión entre JSON y los tipos del dominio. Es interna: el JSON solo cruza la
// frontera como bytes (ADR-11), así que nlohmann::json no aparece en las cabeceras públicas.

#pragma once

#include "mc/core/deck.hpp"
#include "mc/core/deck_validator.hpp"
#include "mc/core/rules_pack.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace mc::core::detail {

/// El mazo recibido no tiene la forma esperada (no es un problema de reglas).
class DeckFormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Entero JSON que cabe en un int; std::nullopt si no es entero o no cabe.
[[nodiscard]] std::optional<int> to_int(const nlohmann::json& value) noexcept;

/// Lee un mazo con la forma {"seeker","legacy","terrain","main":[{"card","copies"}],"side":[...]}.
/// Lanza DeckFormatError si la forma no es válida.
[[nodiscard]] Deck deck_from_json(const nlohmann::json& value);

[[nodiscard]] nlohmann::json issues_to_json(const std::vector<ValidationIssue>& issues);
[[nodiscard]] nlohmann::json pack_info_to_json(const RulesPack& pack);

/// Serializa en UTF-8; cualquier byte no válido se sustituye en vez de lanzar una excepción.
[[nodiscard]] std::string to_text(const nlohmann::json& value);

}  // namespace mc::core::detail
