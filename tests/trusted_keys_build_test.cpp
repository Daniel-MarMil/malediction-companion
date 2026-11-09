// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0
//
// The development key reaches the trusted list only through mc_trust_dev_key with ON
// (cmake/McTrustedKeys.cmake). tests/CMakeLists.txt builds this file twice, each time with
// its own copy of core/src/trusted_keys.cpp and tests/data/test-dev-key.pub as the
// development key: once with ON (MC_TEST_EXPECT_DEV_KEY=1) and once with OFF (=0). The key
// comes from a fixed seed; it is not a real key.

#include "mc/core/trusted_keys.hpp"

#include <gtest/gtest.h>
#include <sodium.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

#ifndef MC_TEST_EXPECT_DEV_KEY
#  error "MC_TEST_EXPECT_DEV_KEY must be 0 or 1"
#endif

namespace {

constexpr std::uint8_t test_dev_seed_byte = 0x5A;

TEST(TrustedKeysBuild, TrustsTheDevelopmentKeyOnlyWithTheOption) {
    ASSERT_GE(sodium_init(), 0);
    std::array<std::uint8_t, crypto_sign_SEEDBYTES> seed{};
    seed.fill(test_dev_seed_byte);
    mc::core::PublicKey public_key{};
    std::array<std::uint8_t, crypto_sign_SECRETKEYBYTES> secret_key{};
    crypto_sign_seed_keypair(public_key.data(), secret_key.data(), seed.data());

    constexpr std::string_view text = R"({"format":"mc-rules-pack"})";
    const std::array<std::uint8_t, text.size()> pack = [&] {
        std::array<std::uint8_t, text.size()> bytes{};
        std::copy(text.begin(), text.end(), bytes.begin());
        return bytes;
    }();
    std::array<std::uint8_t, crypto_sign_BYTES> signature{};
    crypto_sign_detached(signature.data(), nullptr, pack.data(), pack.size(), secret_key.data());

    const auto trusted = mc::core::trusted_keys();
    const bool listed = std::find(trusted.begin(), trusted.end(), public_key) != trusted.end();
    EXPECT_EQ(listed, MC_TEST_EXPECT_DEV_KEY == 1);
    EXPECT_EQ(mc::core::verify_trusted(pack, signature), MC_TEST_EXPECT_DEV_KEY == 1);
}

}  // namespace
