// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0
//
// mc_pack_tool commands (ADR-06): keygen, sign and verify. A development tool: it is not
// part of mc_core, is not exported and is not built for Android.

#pragma once

#include "key_file.hpp"
#include "secure_buffer.hpp"

#include <cstdint>
#include <filesystem>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace mc::pack {

/// A command that cannot finish; the message is for the user.
class ToolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Where passwords come from: the terminal in mc_pack_tool, a fixed value in the tests.
class PasswordSource {
public:
    PasswordSource() = default;
    PasswordSource(const PasswordSource&) = delete;
    PasswordSource& operator=(const PasswordSource&) = delete;
    PasswordSource(PasswordSource&&) = delete;
    PasswordSource& operator=(PasswordSource&&) = delete;
    virtual ~PasswordSource() = default;

    /// Shows the prompt and returns the password in protected memory.
    [[nodiscard]] virtual SecureBuffer read(std::string_view prompt) = 0;
};

inline constexpr std::string_view secret_key_extension = ".mcsk";
inline constexpr std::string_view public_key_extension = ".pub";
inline constexpr std::string_view signature_extension = ".sig";

/// ~/.config/seekers-companion/keys, outside the repository.
[[nodiscard]] std::filesystem::path default_key_dir();

struct KeygenResult {
    std::filesystem::path secret_key_file;
    std::filesystem::path public_key_file;
    std::string public_key_hex;
};

/// Creates <dir>/<name>.mcsk (encrypted, mode 0600) and <dir>/<name>.pub (hexadecimal).
/// Asks for the password twice and never overwrites a file. With an empty seed the key pair
/// is random; the tests pass a fixed seed and low limits.
[[nodiscard]] KeygenResult keygen(const std::filesystem::path& dir, std::string_view name, PasswordSource& passwords,
                                  KdfLimits limits, std::span<const std::uint8_t> seed = {});

/// Signs the pack with the secret key of the file and writes the 64-byte detached signature.
/// Returns the hexadecimal public key of the signer.
std::string sign(const std::filesystem::path& secret_key_file, const std::filesystem::path& pack,
                 const std::filesystem::path& signature, PasswordSource& passwords);

/// Checks the pack and its signature against a hexadecimal public key file with mc_pack_verify.
[[nodiscard]] bool verify(const std::filesystem::path& public_key_file, const std::filesystem::path& pack,
                          const std::filesystem::path& signature);

/// Command line of mc_pack_tool, without the program name. Returns the exit status:
/// 0 on success, 1 on failure or an invalid signature, 2 on a usage error.
/// keygen always uses sensitive_limits().
int run(std::span<const std::string_view> args, PasswordSource& passwords, std::ostream& out, std::ostream& err);

}  // namespace mc::pack
