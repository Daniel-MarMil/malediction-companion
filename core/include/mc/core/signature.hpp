// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace mc::core {

inline constexpr std::size_t signature_size = 64;
inline constexpr std::size_t public_key_size = 32;

/// Comprueba una firma Ed25519 separada (ADR-06). Devuelve false si no coincide
/// o si los tamaños no son los de Ed25519.
[[nodiscard]] bool verify_signature(std::span<const std::uint8_t> message, std::span<const std::uint8_t> signature,
                                    std::span<const std::uint8_t> public_key) noexcept;

}  // namespace mc::core
