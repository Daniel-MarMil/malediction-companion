// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#include "hex.hpp"

#include <sodium.h>

namespace mc::pack {

std::string to_hex(std::span<const std::uint8_t> bytes) {
    std::string hex((bytes.size() * 2) + 1, '\0');
    sodium_bin2hex(hex.data(), hex.size(), bytes.data(), bytes.size());
    hex.pop_back();
    return hex;
}

std::optional<std::vector<std::uint8_t>> try_from_hex(std::string_view hex) {
    if (hex.size() % 2 != 0) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(hex.size() / 2);
    std::size_t written = 0;
    const char* end = nullptr;
    if (sodium_hex2bin(bytes.data(), bytes.size(), hex.data(), hex.size(), nullptr, &written, &end) != 0 ||
        written != bytes.size() || end != hex.data() + hex.size()) {
        return std::nullopt;
    }
    return bytes;
}

}  // namespace mc::pack
