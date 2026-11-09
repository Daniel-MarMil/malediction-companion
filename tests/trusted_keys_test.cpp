// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0
//
// Verification against a list of public keys (ADR-06), with deterministic keys from fixed seeds.

#include "mc/core/trusted_keys.hpp"

#include <gtest/gtest.h>
#include <sodium.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using mc::core::PublicKey;
using mc::core::verify_with_any_key;

using Bytes = std::vector<std::uint8_t>;

struct KeyPair {
    PublicKey public_key{};
    std::array<std::uint8_t, crypto_sign_SECRETKEYBYTES> secret_key{};
};

KeyPair key_pair(std::uint8_t seed_byte) {
    if (sodium_init() < 0) {
        throw std::runtime_error("could not initialize libsodium");
    }
    std::array<std::uint8_t, crypto_sign_SEEDBYTES> seed{};
    seed.fill(seed_byte);
    KeyPair keys;
    crypto_sign_seed_keypair(keys.public_key.data(), keys.secret_key.data(), seed.data());
    return keys;
}

Bytes sign(const Bytes& message, const KeyPair& keys) {
    Bytes signature(crypto_sign_BYTES);
    crypto_sign_detached(signature.data(), nullptr, message.data(), message.size(), keys.secret_key.data());
    return signature;
}

Bytes pack_bytes() {
    constexpr std::string_view text = R"({"format":"mc-rules-pack"})";
    return {text.begin(), text.end()};
}

TEST(TrustedKeys, AcceptsTheSignerInAnyPosition) {
    const KeyPair signer = key_pair(1);
    const Bytes pack = pack_bytes();
    const Bytes signature = sign(pack, signer);
    for (std::size_t position = 0; position < 3; ++position) {
        const PublicKey other_a = key_pair(2).public_key;
        const PublicKey other_b = key_pair(3).public_key;
        std::array<PublicKey, 3> keys{other_a, other_b, other_b};
        keys.at(position) = signer.public_key;
        keys.at(position == 0 ? 1 : 0) = other_a;
        EXPECT_TRUE(verify_with_any_key(pack, signature, keys)) << "signer at position " << position;
    }
}

TEST(TrustedKeys, RejectsWhenNoKeyMatches) {
    const Bytes pack = pack_bytes();
    const Bytes signature = sign(pack, key_pair(1));
    const std::vector<PublicKey> keys = {key_pair(2).public_key, key_pair(3).public_key, key_pair(4).public_key};
    EXPECT_FALSE(verify_with_any_key(pack, signature, keys));
    EXPECT_FALSE(verify_with_any_key(pack, signature, {}));
}

TEST(TrustedKeys, RejectsAModifiedPackOrSignatureWhateverTheKeys) {
    const KeyPair signer = key_pair(1);
    Bytes pack = pack_bytes();
    Bytes signature = sign(pack, signer);
    const std::vector<PublicKey> keys = {key_pair(2).public_key, signer.public_key};

    Bytes modified_pack = pack;
    modified_pack.back() = static_cast<std::uint8_t>('!');
    EXPECT_FALSE(verify_with_any_key(modified_pack, signature, keys));

    signature[0] = static_cast<std::uint8_t>(signature[0] ^ 0x01);
    EXPECT_FALSE(verify_with_any_key(pack, signature, keys));
    EXPECT_FALSE(verify_with_any_key(pack, {}, keys));
}

TEST(TrustedKeys, NoTestKeyIsTrusted) {
    const auto trusted = mc::core::trusted_keys();
    for (const std::uint8_t seed : std::array<std::uint8_t, 6>{1, 2, 3, 4, 7, 8}) {
        const PublicKey key = key_pair(seed).public_key;
        EXPECT_EQ(std::find(trusted.begin(), trusted.end(), key), trusted.end()) << "seed " << int{seed};
    }
}

}  // namespace
