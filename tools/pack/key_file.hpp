// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0
//
// Encrypted secret key file of mc_pack_tool (ADR-06), version 1. It is JSON:
//
//   {"format": "mc-secret-key", "version": 1, "algorithm": "ed25519",
//    "public_key": "<hex>",
//    "kdf": "argon2id13", "opslimit": <n>, "memlimit": <bytes>, "salt": "<hex>",
//    "cipher": "xsalsa20poly1305", "nonce": "<hex>", "ciphertext": "<hex>"}
//
// The encryption key comes from the password with crypto_pwhash (Argon2id) and the salt and
// limits stored in the file; crypto_secretbox encrypts the 64-byte Ed25519 secret key with it.
// Changing any field makes opening fail: the limits, salt, nonce and ciphertext through the
// authentication tag, and the other fields because they must have their exact values.

#pragma once

#include "secure_buffer.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace mc::pack {

/// Argon2id work and memory limits.
struct KdfLimits {
    unsigned long long opslimit;
    std::size_t memlimit;
};

/// The limits keygen always uses: crypto_pwhash_argon2id_OPSLIMIT_SENSITIVE and
/// crypto_pwhash_argon2id_MEMLIMIT_SENSITIVE (1 GiB).
[[nodiscard]] KdfLimits sensitive_limits() noexcept;

/// A key file that cannot be opened: wrong password, altered or unknown format.
class KeyFileError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Encrypts a 64-byte Ed25519 secret key with the password and returns the file text.
[[nodiscard]] std::string seal_secret_key(std::span<const std::uint8_t> secret_key,
                                          std::span<const std::uint8_t> password, KdfLimits limits);

/// Decrypts the secret key of a file into protected memory. Throws KeyFileError.
[[nodiscard]] SecureBuffer open_secret_key(std::string_view file_text, std::span<const std::uint8_t> password);

/// Limits stored in a file, checked against the allowed range. Throws KeyFileError.
[[nodiscard]] KdfLimits stored_limits(std::string_view file_text);

/// Hexadecimal public key stored in a file. Throws KeyFileError.
[[nodiscard]] std::string stored_public_key(std::string_view file_text);

}  // namespace mc::pack
