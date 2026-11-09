// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#include "key_file.hpp"

#include "hex.hpp"

#include <sodium.h>
#include <nlohmann/json.hpp>

#include <array>
#include <utility>
#include <vector>

namespace mc::pack {
namespace {

using nlohmann::json;

constexpr std::string_view format_name = "mc-secret-key";
constexpr int format_version = 1;
constexpr std::string_view algorithm_name = "ed25519";
constexpr std::string_view kdf_name = "argon2id13";
constexpr std::string_view cipher_name = "xsalsa20poly1305";

void ensure_sodium() {
    if (sodium_init() < 0) {
        throw std::runtime_error("could not initialize libsodium");
    }
}

/// Derives the encryption key into protected memory.
SecureBuffer derive_key(std::span<const std::uint8_t> password, std::span<const std::uint8_t> salt, KdfLimits limits) {
    SecureBuffer key(crypto_secretbox_KEYBYTES);
    if (crypto_pwhash(key.data(), key.size(), reinterpret_cast<const char*>(password.data()), password.size(),
                      salt.data(), limits.opslimit, limits.memlimit, crypto_pwhash_ALG_ARGON2ID13) != 0) {
        throw std::runtime_error("Argon2id ran out of memory");
    }
    return key;
}

json parse(std::string_view file_text) {
    json file;
    try {
        file = json::parse(file_text.begin(), file_text.end());
    } catch (const json::exception&) {
        throw KeyFileError("the key file is not valid JSON");
    }
    if (!file.is_object()) {
        throw KeyFileError("the key file is not a JSON object");
    }
    return file;
}

const json& field(const json& file, const char* name) {
    const auto found = file.find(name);
    if (found == file.end()) {
        throw KeyFileError(std::string("the key file has no ") + name);
    }
    return *found;
}

std::string text_field(const json& file, const char* name) {
    const json& value = field(file, name);
    if (!value.is_string()) {
        throw KeyFileError(std::string("the key file field ") + name + " is not text");
    }
    return value.get<std::string>();
}

void expect_text(const json& file, const char* name, std::string_view expected) {
    if (text_field(file, name) != expected) {
        throw KeyFileError(std::string("unsupported key file: ") + name + " must be " + std::string(expected));
    }
}

std::uint64_t number_field(const json& file, const char* name) {
    const json& value = field(file, name);
    if (!value.is_number_unsigned()) {
        throw KeyFileError(std::string("the key file field ") + name + " is not a positive integer");
    }
    return value.get<std::uint64_t>();
}

std::vector<std::uint8_t> bytes_field(const json& file, const char* name, std::size_t size) {
    auto bytes = try_from_hex(text_field(file, name));
    if (bytes && bytes->size() == size) {
        return std::move(*bytes);
    }
    throw KeyFileError(std::string("the key file field ") + name + " must be " + std::to_string(size) +
                       " bytes in hexadecimal");
}

/// Checks the format fields and returns the parsed file.
json checked_file(std::string_view file_text) {
    json file = parse(file_text);
    expect_text(file, "format", format_name);
    if (!field(file, "version").is_number_integer() || field(file, "version").get<int>() != format_version) {
        throw KeyFileError("unsupported key file version");
    }
    expect_text(file, "algorithm", algorithm_name);
    expect_text(file, "kdf", kdf_name);
    expect_text(file, "cipher", cipher_name);
    return file;
}

KdfLimits limits_of(const json& file) {
    // Bounded so that an altered file cannot ask for more work or memory than keygen uses.
    const std::uint64_t opslimit = number_field(file, "opslimit");
    const std::uint64_t memlimit = number_field(file, "memlimit");
    if (opslimit < crypto_pwhash_argon2id_OPSLIMIT_MIN || opslimit > crypto_pwhash_argon2id_OPSLIMIT_SENSITIVE ||
        memlimit < crypto_pwhash_argon2id_MEMLIMIT_MIN || memlimit > crypto_pwhash_argon2id_MEMLIMIT_SENSITIVE) {
        throw KeyFileError("the Argon2id limits of the key file are out of range");
    }
    return {.opslimit = opslimit, .memlimit = static_cast<std::size_t>(memlimit)};
}

}  // namespace

KdfLimits sensitive_limits() noexcept {
    return {.opslimit = crypto_pwhash_argon2id_OPSLIMIT_SENSITIVE,
            .memlimit = crypto_pwhash_argon2id_MEMLIMIT_SENSITIVE};
}

std::string seal_secret_key(std::span<const std::uint8_t> secret_key, std::span<const std::uint8_t> password,
                            KdfLimits limits) {
    ensure_sodium();
    if (secret_key.size() != crypto_sign_SECRETKEYBYTES) {
        throw std::invalid_argument("an Ed25519 secret key has 64 bytes");
    }
    std::array<std::uint8_t, crypto_sign_PUBLICKEYBYTES> public_key{};
    crypto_sign_ed25519_sk_to_pk(public_key.data(), secret_key.data());

    std::array<std::uint8_t, crypto_pwhash_SALTBYTES> salt{};
    std::array<std::uint8_t, crypto_secretbox_NONCEBYTES> nonce{};
    randombytes_buf(salt.data(), salt.size());
    randombytes_buf(nonce.data(), nonce.size());

    const SecureBuffer key = derive_key(password, salt, limits);
    std::vector<std::uint8_t> ciphertext(crypto_secretbox_MACBYTES + secret_key.size());
    crypto_secretbox_easy(ciphertext.data(), secret_key.data(), secret_key.size(), nonce.data(), key.data());

    json file = json::object();
    file["format"] = format_name;
    file["version"] = format_version;
    file["algorithm"] = algorithm_name;
    file["public_key"] = to_hex(public_key);
    file["kdf"] = kdf_name;
    file["opslimit"] = static_cast<std::uint64_t>(limits.opslimit);
    file["memlimit"] = static_cast<std::uint64_t>(limits.memlimit);
    file["salt"] = to_hex(salt);
    file["cipher"] = cipher_name;
    file["nonce"] = to_hex(nonce);
    file["ciphertext"] = to_hex(ciphertext);
    return file.dump(2) + "\n";
}

SecureBuffer open_secret_key(std::string_view file_text, std::span<const std::uint8_t> password) {
    ensure_sodium();
    const json file = checked_file(file_text);
    const KdfLimits limits = limits_of(file);
    const auto public_key = bytes_field(file, "public_key", crypto_sign_PUBLICKEYBYTES);
    const auto salt = bytes_field(file, "salt", crypto_pwhash_SALTBYTES);
    const auto nonce = bytes_field(file, "nonce", crypto_secretbox_NONCEBYTES);
    const auto ciphertext = bytes_field(file, "ciphertext", crypto_secretbox_MACBYTES + crypto_sign_SECRETKEYBYTES);

    const SecureBuffer key = derive_key(password, salt, limits);
    SecureBuffer secret_key(crypto_sign_SECRETKEYBYTES);
    if (crypto_secretbox_open_easy(secret_key.data(), ciphertext.data(), ciphertext.size(), nonce.data(), key.data()) !=
        0) {
        throw KeyFileError("wrong password, or the key file was altered");
    }
    std::array<std::uint8_t, crypto_sign_PUBLICKEYBYTES> derived{};
    crypto_sign_ed25519_sk_to_pk(derived.data(), secret_key.data());
    if (sodium_memcmp(derived.data(), public_key.data(), derived.size()) != 0) {
        throw KeyFileError("the public key of the key file does not match its secret key");
    }
    return secret_key;
}

KdfLimits stored_limits(std::string_view file_text) {
    return limits_of(checked_file(file_text));
}

std::string stored_public_key(std::string_view file_text) {
    return to_hex(bytes_field(checked_file(file_text), "public_key", crypto_sign_PUBLICKEYBYTES));
}

}  // namespace mc::pack
