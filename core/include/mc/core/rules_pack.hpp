// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "mc/core/card.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mc::core {

/// Parámetros de construcción del mazo. Vienen del paquete, no del código (ADR-06),
/// para seguir las revisiones del reglamento sin publicar la app.
struct DeckRules {
    int deck_min{30};
    int deck_max{50};
    int side_max{7};
    /// Copias máximas por rango. Un rango sin entrada no puede ir en el mazo: la carta
    /// legendaria es el propio Seeker.
    std::map<Rank, int> copy_limits{{Rank::Basic, 3}, {Rank::Elite, 2}, {Rank::Unique, 1}};
    /// Si las cartas sin facción valen para cualquier mazo (pendiente de confirmar).
    bool unaligned_cards_allowed{true};
    /// Si las copias del mazo auxiliar suman a las del principal para el límite
    /// (pendiente de confirmar). Si no, cada mazo cumple el límite por separado.
    bool copy_limits_include_side{true};
};

/// Metadatos del paquete (RF-02).
struct PackInfo {
    std::string id;
    std::string version;
    std::string date;
    std::string origin;
    std::string license;
};

/// Error al leer un paquete de reglas.
class PackError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Paquete de reglas inmutable: metadatos, reglas de mazo y cartas.
class RulesPack {
public:
    /// Lee un paquete en JSON. Lanza PackError si no es válido.
    [[nodiscard]] static RulesPack parse(std::string_view text);

    [[nodiscard]] const PackInfo& info() const noexcept { return info_; }
    [[nodiscard]] const DeckRules& deck_rules() const noexcept { return rules_; }
    [[nodiscard]] const std::vector<Card>& cards() const noexcept { return cards_; }
    [[nodiscard]] std::size_t card_count() const noexcept { return cards_.size(); }

    /// Busca una carta por su identificador; devuelve nullptr si no existe.
    [[nodiscard]] const Card* find(std::string_view id) const noexcept;

private:
    PackInfo info_;
    DeckRules rules_;
    std::vector<Card> cards_;
    std::map<std::string, std::size_t, std::less<>> index_;
};

}  // namespace mc::core
