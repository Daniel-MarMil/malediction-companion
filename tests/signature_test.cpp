// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#include "mc/core/signature.hpp"

#include <gtest/gtest.h>
#include <sodium.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using mc::core::verify_signature;

using Bytes = std::vector<std::uint8_t>;

Bytes from_hex(std::string_view hex) {
    Bytes bytes(hex.size() / 2);
    std::size_t written = 0;
    if (sodium_hex2bin(bytes.data(), bytes.size(), hex.data(), hex.size(), nullptr, &written, nullptr) != 0 ||
        written != bytes.size()) {
        throw std::invalid_argument("invalid hexadecimal");
    }
    return bytes;
}

Bytes from_text(std::string_view text) {
    return {text.begin(), text.end()};
}

struct KeyPair {
    Bytes public_key = Bytes(crypto_sign_PUBLICKEYBYTES);
    Bytes secret_key = Bytes(crypto_sign_SECRETKEYBYTES);
};

/// Deterministic keys from a seed, so that the tests are repeatable.
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

// Test vector 1 from RFC 8032, section 7.1: an empty message.
TEST(Signature, MatchesRfc8032TestVector) {
    const Bytes public_key = from_hex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
    const Bytes signature = from_hex(
        "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe"
        "24655141438e7a100b");
    EXPECT_TRUE(verify_signature({}, signature, public_key));
    EXPECT_FALSE(verify_signature(from_text("x"), signature, public_key));
}

TEST(Signature, AcceptsAValidSignature) {
    const KeyPair keys = key_pair(7);
    const Bytes pack = from_text(R"({"format":"mc-rules-pack"})");
    EXPECT_TRUE(verify_signature(pack, sign(pack, keys), keys.public_key));
}

TEST(Signature, RejectsAModifiedPack) {
    const KeyPair keys = key_pair(7);
    Bytes pack = from_text(R"({"format":"mc-rules-pack"})");
    const Bytes signature = sign(pack, keys);
    pack.back() = static_cast<std::uint8_t>('!');
    EXPECT_FALSE(verify_signature(pack, signature, keys.public_key));
}

TEST(Signature, RejectsAModifiedSignature) {
    const KeyPair keys = key_pair(7);
    const Bytes pack = from_text("pack");
    Bytes signature = sign(pack, keys);
    signature[10] = static_cast<std::uint8_t>(signature[10] ^ 0x01);
    EXPECT_FALSE(verify_signature(pack, signature, keys.public_key));
}

TEST(Signature, RejectsAnotherKey) {
    const Bytes pack = from_text("pack");
    const Bytes signature = sign(pack, key_pair(7));
    EXPECT_FALSE(verify_signature(pack, signature, key_pair(8).public_key));
}

TEST(Signature, RejectsWrongSizes) {
    const KeyPair keys = key_pair(7);
    const Bytes pack = from_text("pack");
    Bytes signature = sign(pack, keys);
    Bytes short_signature(signature.begin(), signature.end() - 1);
    Bytes short_key(keys.public_key.begin(), keys.public_key.end() - 1);
    EXPECT_FALSE(verify_signature(pack, short_signature, keys.public_key));
    EXPECT_FALSE(verify_signature(pack, signature, short_key));
    EXPECT_FALSE(verify_signature(pack, {}, {}));
}

}  // namespace
