// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mc::pack {

/// Lowercase hexadecimal. Only for data that is not secret.
[[nodiscard]] std::string to_hex(std::span<const std::uint8_t> bytes);

/// Reads hexadecimal digits, upper or lower case. Returns nothing on anything else.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> try_from_hex(std::string_view hex);

}  // namespace mc::pack
