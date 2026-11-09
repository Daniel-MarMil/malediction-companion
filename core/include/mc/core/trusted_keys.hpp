// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "mc/core/signature.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace mc::core {

using PublicKey = std::array<std::uint8_t, public_key_size>;

/// Public keys compiled into the core whose signatures it accepts (ADR-06): the release
/// keys and, only in Debug builds with MC_TRUST_DEV_KEY, the development key.
[[nodiscard]] std::span<const PublicKey> trusted_keys() noexcept;

/// True if the signature is valid for any of the keys, in any position.
[[nodiscard]] bool verify_with_any_key(std::span<const std::uint8_t> message, std::span<const std::uint8_t> signature,
                                       std::span<const PublicKey> keys) noexcept;

/// True if the signature is valid for one of the trusted keys.
[[nodiscard]] bool verify_trusted(std::span<const std::uint8_t> message,
                                  std::span<const std::uint8_t> signature) noexcept;

}  // namespace mc::core
