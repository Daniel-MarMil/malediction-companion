// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#include "mc/core/signature.hpp"

#include <sodium.h>

namespace mc::core {

static_assert(signature_size == crypto_sign_BYTES);
static_assert(public_key_size == crypto_sign_PUBLICKEYBYTES);

bool verify_signature(std::span<const std::uint8_t> message, std::span<const std::uint8_t> signature,
                      std::span<const std::uint8_t> public_key) noexcept {
    if (signature.size() != signature_size || public_key.size() != public_key_size) {
        return false;
    }
    // sodium_init admite llamadas repetidas y desde varios hilos; basta con hacerlo una vez.
    static const bool sodium_ready = sodium_init() >= 0;
    if (!sodium_ready) {
        return false;
    }
    // Un mensaje vacío puede llegar sin puntero; libsodium recibe uno válido de todos modos.
    static constexpr unsigned char empty_message = 0;
    const unsigned char* data = message.empty() ? &empty_message : message.data();
    return crypto_sign_verify_detached(signature.data(), data, message.size(), public_key.data()) == 0;
}

}  // namespace mc::core
