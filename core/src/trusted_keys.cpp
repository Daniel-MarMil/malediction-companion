// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0
//
// Public keys compiled into the core (ADR-06). The list is short and only changes with a
// release of the app. CMake (cmake/McTrustedKeys.cmake) decides whether the development key
// is included: only Debug builds with MC_TRUST_DEV_KEY get it, through MC_DEV_PUBLIC_KEY_HEX.

#include "mc/core/trusted_keys.hpp"

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace mc::core {
namespace {

constexpr std::uint8_t hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<std::uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<std::uint8_t>(c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<std::uint8_t>(c - 'A' + 10);
    }
    throw "a public key may only hold hexadecimal digits";  // Only evaluated at compile time.
}

/// Turns 64 hexadecimal digits into a key at compile time; anything else does not compile.
// Unused while there is no development key and no release key.
[[maybe_unused]] consteval PublicKey key_from_hex(std::string_view hex) {
    if (hex.size() != 2 * public_key_size) {
        throw "a public key has exactly 64 hexadecimal digits";
    }
    PublicKey key{};
    std::size_t next = 0;
    for (std::uint8_t& byte : key) {
        const std::uint8_t high = hex_digit(hex[next]);
        const std::uint8_t low = hex_digit(hex[next + 1]);
        byte = static_cast<std::uint8_t>((high << 4U) | low);
        next += 2;
    }
    return key;
}

// Release keys. None exist yet: they are added here, with their hexadecimal value, when
// the first release pack is signed.
constexpr std::array<PublicKey, 0> release_keys{};

#ifdef MC_DEV_PUBLIC_KEY_HEX
constexpr std::array<PublicKey, release_keys.size() + 1> keys = [] {
    std::array<PublicKey, release_keys.size() + 1> all{};
    std::ranges::copy(release_keys, all.begin());
    all.back() = key_from_hex(MC_DEV_PUBLIC_KEY_HEX);
    return all;
}();
#else
constexpr auto keys = release_keys;
#endif

}  // namespace

std::span<const PublicKey> trusted_keys() noexcept {
    return keys;
}

bool verify_with_any_key(std::span<const std::uint8_t> message, std::span<const std::uint8_t> signature,
                         std::span<const PublicKey> keys_to_try) noexcept {
    return std::ranges::any_of(keys_to_try,
                               [&](const PublicKey& key) { return verify_signature(message, signature, key); });
}

bool verify_trusted(std::span<const std::uint8_t> message, std::span<const std::uint8_t> signature) noexcept {
    return verify_with_any_key(message, signature, trusted_keys());
}

}  // namespace mc::core
