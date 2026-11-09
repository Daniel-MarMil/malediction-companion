// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#include "pack_tool.hpp"

#include "hex.hpp"
#include "mc/mc_api.h"

#include <fcntl.h>
#include <sodium.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <vector>

namespace mc::pack {
namespace {

namespace fs = std::filesystem;

// The same limit as mc_session_create; key files are a few hundred bytes.
constexpr std::size_t max_pack_bytes = std::size_t{16} * 1024 * 1024;
constexpr std::size_t max_small_file_bytes = std::size_t{64} * 1024;

constexpr std::string_view usage_text =
    "Usage:\n"
    "  mc_pack_tool keygen NAME [--dir DIR]\n"
    "      Creates DIR/NAME.mcsk (secret key, encrypted with a password) and DIR/NAME.pub\n"
    "      (public key, in hexadecimal). DIR defaults to ~/.config/seekers-companion/keys.\n"
    "      It never overwrites a file.\n"
    "  mc_pack_tool sign SECRET_KEY PACK [--out SIGNATURE]\n"
    "      Writes the 64-byte detached signature of PACK, by default to PACK.sig.\n"
    "  mc_pack_tool verify PUBLIC_KEY PACK [SIGNATURE]\n"
    "      Checks PACK and its signature (by default PACK.sig) with mc_pack_verify.\n"
    "Passwords are always read from the terminal, never from arguments or the environment.\n";

[[noreturn]] void fail_errno(const std::string& what, const fs::path& path) {
    throw ToolError(what + " " + path.string() + ": " + std::generic_category().message(errno));
}

std::string read_file(const fs::path& path, std::size_t max_bytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw ToolError("cannot open " + path.string());
    }
    std::string content;
    std::array<char, 4096> chunk{};
    while (file.read(chunk.data(), chunk.size()) || file.gcount() > 0) {
        content.append(chunk.data(), static_cast<std::size_t>(file.gcount()));
        if (content.size() > max_bytes) {
            throw ToolError(path.string() + " is larger than " + std::to_string(max_bytes) + " bytes");
        }
    }
    if (file.bad()) {
        throw ToolError("cannot read " + path.string());
    }
    return content;
}

void write_all(int fd, std::string_view data, const fs::path& path) {
    while (!data.empty()) {
        const ssize_t written = ::write(fd, data.data(), data.size());
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            fail_errno("cannot write", path);
        }
        data.remove_prefix(static_cast<std::size_t>(written));
    }
}

/// Writes a file with the given mode. With exclusive, it fails if the file exists.
void write_file(const fs::path& path, std::string_view data, mode_t mode, bool exclusive) {
    const int flags = O_WRONLY | O_CREAT | O_CLOEXEC | (exclusive ? O_EXCL : O_TRUNC);
    const int fd = ::open(path.c_str(), flags, mode);  // NOLINT(cppcoreguidelines-pro-type-vararg)
    if (fd < 0) {
        fail_errno(exclusive ? "cannot create (it must not exist)" : "cannot create", path);
    }
    try {
        write_all(fd, data, path);
        if (::fsync(fd) != 0) {
            fail_errno("cannot write", path);
        }
    } catch (...) {
        ::close(fd);
        throw;
    }
    if (::close(fd) != 0) {
        fail_errno("cannot write", path);
    }
}

std::span<const std::uint8_t> as_bytes(std::string_view text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

bool valid_name(std::string_view name) {
    return !name.empty() && name.front() != '.' && std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
               c == '.';
    });
}

SecureBuffer read_new_password(PasswordSource& passwords) {
    SecureBuffer first = passwords.read("New password: ");
    if (first.size() == 0) {
        throw ToolError("the password cannot be empty");
    }
    const SecureBuffer second = passwords.read("Repeat the password: ");
    if (!same_secret(first.bytes(), second.bytes())) {
        throw ToolError("the passwords do not match");
    }
    return first;
}

std::vector<std::uint8_t> read_public_key(const fs::path& path) {
    std::string text = read_file(path, max_small_file_bytes);
    const auto is_space = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    std::erase_if(text, [&](char c) { return is_space(static_cast<unsigned char>(c)); });
    auto key = try_from_hex(text);
    if (key && key->size() == crypto_sign_PUBLICKEYBYTES) {
        return std::move(*key);
    }
    throw ToolError(path.string() + " does not hold an Ed25519 public key in hexadecimal");
}

/// Splits the arguments into positional ones and the value of one optional "--name VALUE".
struct Arguments {
    std::vector<std::string_view> positional;
    std::optional<std::string_view> option;
};

std::optional<Arguments> parse_arguments(std::span<const std::string_view> args, std::string_view option_name) {
    Arguments parsed;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == option_name && !option_name.empty()) {
            if (i + 1 == args.size() || parsed.option) {
                return std::nullopt;
            }
            parsed.option = args[++i];
        } else if (args[i].starts_with("--")) {
            return std::nullopt;
        } else {
            parsed.positional.push_back(args[i]);
        }
    }
    return parsed;
}

int usage(std::ostream& err) {
    err << usage_text;
    return 2;
}

int run_keygen(std::span<const std::string_view> args, PasswordSource& passwords, std::ostream& out,
               std::ostream& err) {
    const auto parsed = parse_arguments(args, "--dir");
    if (!parsed || parsed->positional.size() != 1) {
        return usage(err);
    }
    const fs::path dir = parsed->option ? fs::path(*parsed->option) : default_key_dir();
    out << "keygen asks for the password twice; then Argon2id with 1 GiB of memory takes a few seconds.\n";
    const KeygenResult result = keygen(dir, parsed->positional[0], passwords, sensitive_limits());
    out << "Secret key: " << result.secret_key_file.string() << "\n"
        << "Public key: " << result.public_key_file.string() << "\n"
        << result.public_key_hex << "\n"
        << "Keep a backup of the secret key and its password offline; neither goes into the repository.\n";
    return 0;
}

int run_sign(std::span<const std::string_view> args, PasswordSource& passwords, std::ostream& out, std::ostream& err) {
    const auto parsed = parse_arguments(args, "--out");
    if (!parsed || parsed->positional.size() != 2) {
        return usage(err);
    }
    const fs::path pack(parsed->positional[1]);
    const fs::path signature =
        parsed->option ? fs::path(*parsed->option) : fs::path(pack.string() + std::string(signature_extension));
    const std::string public_key = sign(fs::path(parsed->positional[0]), pack, signature, passwords);
    out << "Signature: " << signature.string() << "\n"
        << "Public key: " << public_key << "\n";
    return 0;
}

int run_verify(std::span<const std::string_view> args, std::ostream& out, std::ostream& err) {
    const auto parsed = parse_arguments(args, "");
    if (!parsed || parsed->positional.size() < 2 || parsed->positional.size() > 3) {
        return usage(err);
    }
    const fs::path pack(parsed->positional[1]);
    const fs::path signature = parsed->positional.size() == 3
                                   ? fs::path(parsed->positional[2])
                                   : fs::path(pack.string() + std::string(signature_extension));
    if (verify(fs::path(parsed->positional[0]), pack, signature)) {
        out << "Valid signature\n";
        return 0;
    }
    out << "Invalid signature\n";
    return 1;
}

}  // namespace

fs::path default_key_dir() {
    const char* home = std::getenv("HOME");  // NOLINT(concurrency-mt-unsafe): read once, single thread.
    if (home == nullptr || *home == '\0') {
        throw ToolError("HOME is not set; choose a folder with --dir");
    }
    return fs::path(home) / ".config" / "seekers-companion" / "keys";
}

KeygenResult keygen(const fs::path& dir, std::string_view name, PasswordSource& passwords, KdfLimits limits,
                    std::span<const std::uint8_t> seed) {
    if (sodium_init() < 0) {
        throw ToolError("could not initialize libsodium");
    }
    if (!valid_name(name)) {
        throw ToolError("the key name may only hold letters, digits, '-', '_' and '.', and cannot start with '.'");
    }
    if (!seed.empty() && seed.size() != crypto_sign_SEEDBYTES) {
        throw std::invalid_argument("an Ed25519 seed has 32 bytes");
    }
    KeygenResult result{.secret_key_file = dir / (std::string(name) + std::string(secret_key_extension)),
                        .public_key_file = dir / (std::string(name) + std::string(public_key_extension)),
                        .public_key_hex = {}};
    for (const fs::path& path : {result.secret_key_file, result.public_key_file}) {
        if (fs::exists(fs::symlink_status(path))) {
            throw ToolError(path.string() + " already exists; keygen never overwrites keys");
        }
    }

    const SecureBuffer password = read_new_password(passwords);

    SecureBuffer secret_key(crypto_sign_SECRETKEYBYTES);
    std::array<std::uint8_t, crypto_sign_PUBLICKEYBYTES> public_key{};
    if (seed.empty()) {
        crypto_sign_keypair(public_key.data(), secret_key.data());
    } else {
        crypto_sign_seed_keypair(public_key.data(), secret_key.data(), seed.data());
    }
    const std::string sealed = seal_secret_key(secret_key.bytes(), password.bytes(), limits);
    result.public_key_hex = to_hex(public_key);

    if (fs::create_directories(dir)) {
        fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace);
    }
    write_file(result.secret_key_file, sealed, S_IRUSR | S_IWUSR, true);
    try {
        write_file(result.public_key_file, result.public_key_hex + "\n", S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, true);
    } catch (...) {
        std::error_code ignored;
        fs::remove(result.secret_key_file, ignored);
        throw;
    }
    return result;
}

std::string sign(const fs::path& secret_key_file, const fs::path& pack, const fs::path& signature,
                 PasswordSource& passwords) {
    if (sodium_init() < 0) {
        throw ToolError("could not initialize libsodium");
    }
    const std::string key_text = read_file(secret_key_file, max_small_file_bytes);
    const std::string pack_bytes = read_file(pack, max_pack_bytes);
    std::array<std::uint8_t, crypto_sign_BYTES> detached{};
    std::array<std::uint8_t, crypto_sign_PUBLICKEYBYTES> public_key{};
    try {
        (void)stored_limits(key_text);  // A broken file fails before asking for the password.
        const SecureBuffer password = passwords.read("Password of " + secret_key_file.filename().string() + ": ");
        const SecureBuffer secret_key = open_secret_key(key_text, password.bytes());
        const auto message = as_bytes(pack_bytes);
        crypto_sign_detached(detached.data(), nullptr, message.data(), message.size(), secret_key.data());
        crypto_sign_ed25519_sk_to_pk(public_key.data(), secret_key.data());
    } catch (const KeyFileError& error) {
        throw ToolError(secret_key_file.string() + ": " + error.what());
    }
    // The signature must pass the same check the apps run.
    const auto message = as_bytes(pack_bytes);
    if (mc_pack_verify(message.data(), message.size(), detached.data(), detached.size(), public_key.data(),
                       public_key.size()) != MC_OK) {
        throw ToolError("the new signature does not pass mc_pack_verify");
    }
    write_file(signature, std::string_view(reinterpret_cast<const char*>(detached.data()), detached.size()),
               S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, false);
    return to_hex(public_key);
}

bool verify(const fs::path& public_key_file, const fs::path& pack, const fs::path& signature) {
    const std::vector<std::uint8_t> public_key = read_public_key(public_key_file);
    const std::string pack_bytes = read_file(pack, max_pack_bytes);
    const std::string signature_bytes = read_file(signature, max_small_file_bytes);
    const auto message = as_bytes(pack_bytes);
    const auto detached = as_bytes(signature_bytes);
    return mc_pack_verify(message.data(), message.size(), detached.data(), detached.size(), public_key.data(),
                          public_key.size()) == MC_OK;
}

int run(std::span<const std::string_view> args, PasswordSource& passwords, std::ostream& out, std::ostream& err) {
    if (args.empty()) {
        return usage(err);
    }
    const std::string_view command = args.front();
    const auto rest = args.subspan(1);
    try {
        if (command == "keygen") {
            return run_keygen(rest, passwords, out, err);
        }
        if (command == "sign") {
            return run_sign(rest, passwords, out, err);
        }
        if (command == "verify") {
            return run_verify(rest, out, err);
        }
        if (command == "help" || command == "--help" || command == "-h") {
            out << usage_text;
            return 0;
        }
        return usage(err);
    } catch (const std::exception& error) {
        err << "mc_pack_tool: " << error.what() << "\n";
        return 1;
    }
}

}  // namespace mc::pack
