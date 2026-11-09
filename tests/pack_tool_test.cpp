// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0
//
// mc_pack_tool (ADR-06): keygen, sign and verify end to end, with keys from fixed seeds and
// the lowest Argon2id limits, so the tests are quick and do not need 1 GiB of memory.
// keygen from the command line always uses the sensitive limits; it is not run here.

#include "pack_tool.hpp"
#include "key_file.hpp"
#include "secure_buffer.hpp"

#include "mc/mc_api.h"

#include <gtest/gtest.h>
#include <sodium.h>
#include <sys/stat.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using mc::pack::KdfLimits;
using mc::pack::KeyFileError;
using mc::pack::SecureBuffer;
using mc::pack::ToolError;
using nlohmann::json;

constexpr KdfLimits low_limits{.opslimit = crypto_pwhash_argon2id_OPSLIMIT_MIN,
                               .memlimit = crypto_pwhash_argon2id_MEMLIMIT_MIN};
constexpr std::string_view password = "correct horse battery staple";

/// Returns the queued passwords in order; fails the test if a command asks for more.
class FixedPasswords final : public mc::pack::PasswordSource {
public:
    explicit FixedPasswords(std::initializer_list<std::string_view> passwords) : queue_(passwords) {}

    SecureBuffer read(std::string_view /*prompt*/) override {
        if (queue_.empty()) {
            throw std::logic_error("no password left in the test");
        }
        const std::string_view next = queue_.front();
        queue_.pop_front();
        SecureBuffer buffer(next.size());
        std::copy(next.begin(), next.end(), buffer.data());
        ++asked_;
        return buffer;
    }

    [[nodiscard]] int asked() const { return asked_; }

private:
    std::deque<std::string_view> queue_;
    int asked_ = 0;
};

std::array<std::uint8_t, crypto_sign_SEEDBYTES> seed(std::uint8_t byte) {
    std::array<std::uint8_t, crypto_sign_SEEDBYTES> result{};
    result.fill(byte);
    return result;
}

std::string expected_public_key_hex(std::uint8_t seed_byte) {
    const auto bytes = seed(seed_byte);
    std::array<std::uint8_t, crypto_sign_PUBLICKEYBYTES> public_key{};
    std::array<std::uint8_t, crypto_sign_SECRETKEYBYTES> secret_key{};
    crypto_sign_seed_keypair(public_key.data(), secret_key.data(), bytes.data());
    std::array<char, (2 * crypto_sign_PUBLICKEYBYTES) + 1> hex{};
    sodium_bin2hex(hex.data(), hex.size(), public_key.data(), public_key.size());
    return {hex.data()};
}

std::string read_text(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void write_text(const fs::path& path, std::string_view text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::span<const std::uint8_t> as_bytes(std::string_view text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

/// Changes the first hexadecimal digit, so the value stays valid hexadecimal.
std::string flip_first_digit(std::string hex) {
    hex.front() = hex.front() == '0' ? '1' : '0';
    return hex;
}

class PackTool : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_GE(sodium_init(), 0);
        std::array<std::uint8_t, 8> random{};
        randombytes_buf(random.data(), random.size());
        std::string suffix;
        for (const auto byte : random) {
            suffix += "0123456789abcdef"[byte % 16];
        }
        dir_ = fs::temp_directory_path() / ("mc_pack_tool_test_" + suffix);
        fs::create_directories(dir_);
        pack_ = dir_ / "pack.json";
        write_text(pack_, R"({"format":"mc-rules-pack","version":1})");
    }

    void TearDown() override {
        std::error_code ignored;
        fs::remove_all(dir_, ignored);
    }

    mc::pack::KeygenResult make_key(std::uint8_t seed_byte = 0x11, std::string_view name = "dev") {
        FixedPasswords passwords{password, password};
        const auto bytes = seed(seed_byte);
        return mc::pack::keygen(dir_ / "keys", name, passwords, low_limits, bytes);
    }

    [[nodiscard]] fs::path signature() const { return dir_ / "pack.json.sig"; }

    fs::path dir_;
    fs::path pack_;
};

TEST_F(PackTool, KeygenSignAndVerifyEndToEnd) {
    const auto keys = make_key(0x11);
    EXPECT_EQ(keys.secret_key_file, dir_ / "keys" / "dev.mcsk");
    EXPECT_EQ(keys.public_key_file, dir_ / "keys" / "dev.pub");
    EXPECT_EQ(keys.public_key_hex, expected_public_key_hex(0x11));
    EXPECT_EQ(read_text(keys.public_key_file), keys.public_key_hex + "\n");

    // Only the owner can read the secret key, and the new folder is the owner's only.
    struct stat info{};
    ASSERT_EQ(::stat(keys.secret_key_file.c_str(), &info), 0);
    EXPECT_EQ(info.st_mode & 0777U, 0600U);
    ASSERT_EQ(::stat((dir_ / "keys").c_str(), &info), 0);
    EXPECT_EQ(info.st_mode & 0777U, 0700U);

    // The seed, which is the first half of the secret key, never appears in the file in clear.
    const auto seed_bytes = seed(0x11);
    std::array<char, (2 * crypto_sign_SEEDBYTES) + 1> seed_hex{};
    sodium_bin2hex(seed_hex.data(), seed_hex.size(), seed_bytes.data(), seed_bytes.size());
    EXPECT_EQ(read_text(keys.secret_key_file).find(seed_hex.data()), std::string::npos);

    FixedPasswords passwords{password};
    EXPECT_EQ(mc::pack::sign(keys.secret_key_file, pack_, signature(), passwords), keys.public_key_hex);
    EXPECT_EQ(passwords.asked(), 1);
    const std::string detached = read_text(signature());
    ASSERT_EQ(detached.size(), std::size_t{crypto_sign_BYTES});

    EXPECT_TRUE(mc::pack::verify(keys.public_key_file, pack_, signature()));
    // Ed25519 is deterministic: it is exactly the signature of that seed's key, which the apps
    // check with mc_pack_verify.
    const std::string pack = read_text(pack_);
    std::array<std::uint8_t, crypto_sign_PUBLICKEYBYTES> key{};
    std::array<std::uint8_t, crypto_sign_SECRETKEYBYTES> secret{};
    crypto_sign_seed_keypair(key.data(), secret.data(), seed_bytes.data());
    std::array<std::uint8_t, crypto_sign_BYTES> expected{};
    crypto_sign_detached(expected.data(), nullptr, as_bytes(pack).data(), pack.size(), secret.data());
    EXPECT_EQ(detached, std::string(expected.begin(), expected.end()));
    EXPECT_EQ(mc_pack_verify(as_bytes(pack).data(), pack.size(), as_bytes(detached).data(), detached.size(), key.data(),
                             key.size()),
              MC_OK);
}

TEST_F(PackTool, KeyFileStoresItsFormatAndLimits) {
    const auto keys = make_key();
    const json file = json::parse(read_text(keys.secret_key_file));
    EXPECT_EQ(file.at("format"), "mc-secret-key");
    EXPECT_EQ(file.at("version"), 1);
    EXPECT_EQ(file.at("algorithm"), "ed25519");
    EXPECT_EQ(file.at("kdf"), "argon2id13");
    EXPECT_EQ(file.at("cipher"), "xsalsa20poly1305");
    EXPECT_EQ(file.at("opslimit"), low_limits.opslimit);
    EXPECT_EQ(file.at("memlimit"), low_limits.memlimit);
    EXPECT_EQ(file.at("salt").get<std::string>().size(), 2U * crypto_pwhash_SALTBYTES);
    EXPECT_EQ(file.at("nonce").get<std::string>().size(), 2U * crypto_secretbox_NONCEBYTES);

    const KdfLimits stored = mc::pack::stored_limits(read_text(keys.secret_key_file));
    EXPECT_EQ(stored.opslimit, low_limits.opslimit);
    EXPECT_EQ(stored.memlimit, low_limits.memlimit);
    EXPECT_EQ(mc::pack::stored_public_key(read_text(keys.secret_key_file)), keys.public_key_hex);
}

TEST_F(PackTool, OtherLimitsAreStoredAndUsedToOpen) {
    const KdfLimits limits{.opslimit = crypto_pwhash_argon2id_OPSLIMIT_MIN + 1,
                           .memlimit = 2 * crypto_pwhash_argon2id_MEMLIMIT_MIN};
    std::array<std::uint8_t, crypto_sign_PUBLICKEYBYTES> public_key{};
    SecureBuffer secret_key(crypto_sign_SECRETKEYBYTES);
    crypto_sign_keypair(public_key.data(), secret_key.data());
    const std::string text = mc::pack::seal_secret_key(secret_key.bytes(), as_bytes(password), limits);
    const KdfLimits stored = mc::pack::stored_limits(text);
    EXPECT_EQ(stored.opslimit, limits.opslimit);
    EXPECT_EQ(stored.memlimit, limits.memlimit);
    const SecureBuffer opened = mc::pack::open_secret_key(text, as_bytes(password));
    EXPECT_TRUE(mc::pack::same_secret(opened.bytes(), secret_key.bytes()));
}

TEST(PackToolLimits, KeygenUsesTheSensitiveLimits) {
    const KdfLimits limits = mc::pack::sensitive_limits();
    EXPECT_EQ(limits.opslimit, crypto_pwhash_argon2id_OPSLIMIT_SENSITIVE);
    EXPECT_EQ(limits.memlimit, crypto_pwhash_argon2id_MEMLIMIT_SENSITIVE);
    EXPECT_EQ(limits.memlimit, std::size_t{1} << 30U);
}

TEST_F(PackTool, KeygenAsksTwiceAndRejectsDifferentPasswords) {
    FixedPasswords passwords{password, "something else"};
    const auto bytes = seed(0x11);
    EXPECT_THROW((void)mc::pack::keygen(dir_ / "keys", "dev", passwords, low_limits, bytes), ToolError);
    EXPECT_EQ(passwords.asked(), 2);
    EXPECT_FALSE(fs::exists(dir_ / "keys" / "dev.mcsk"));
    EXPECT_FALSE(fs::exists(dir_ / "keys" / "dev.pub"));
}

TEST_F(PackTool, KeygenRejectsAnEmptyPassword) {
    FixedPasswords passwords{"", ""};
    const auto bytes = seed(0x11);
    EXPECT_THROW((void)mc::pack::keygen(dir_ / "keys", "dev", passwords, low_limits, bytes), ToolError);
    EXPECT_FALSE(fs::exists(dir_ / "keys" / "dev.mcsk"));
}

TEST_F(PackTool, KeygenNeverOverwrites) {
    const auto keys = make_key(0x11);
    const std::string secret_before = read_text(keys.secret_key_file);
    const std::string public_before = read_text(keys.public_key_file);
    EXPECT_THROW(make_key(0x22), ToolError);
    EXPECT_EQ(read_text(keys.secret_key_file), secret_before);
    EXPECT_EQ(read_text(keys.public_key_file), public_before);

    // A public key alone also blocks it, and nothing is written.
    write_text(dir_ / "keys" / "other.pub", "x");
    EXPECT_THROW(make_key(0x22, "other"), ToolError);
    EXPECT_FALSE(fs::exists(dir_ / "keys" / "other.mcsk"));
    EXPECT_EQ(read_text(dir_ / "keys" / "other.pub"), "x");
}

TEST_F(PackTool, KeygenRejectsNamesThatAreNotPlain) {
    for (const std::string_view name : {"", ".hidden", "../dev", "a/b", "dev key"}) {
        EXPECT_THROW(make_key(0x11, name), ToolError) << name;
    }
}

TEST_F(PackTool, SignRejectsAWrongPassword) {
    const auto keys = make_key();
    FixedPasswords passwords{"wrong password"};
    EXPECT_THROW(mc::pack::sign(keys.secret_key_file, pack_, signature(), passwords), ToolError);
    EXPECT_FALSE(fs::exists(signature()));
}

TEST_F(PackTool, SignRejectsAnAlteredKeyFile) {
    const auto keys = make_key();
    const json original = json::parse(read_text(keys.secret_key_file));

    std::vector<std::pair<std::string, json>> changes;
    for (const char* field : {"salt", "nonce", "ciphertext", "public_key"}) {
        changes.emplace_back(field, flip_first_digit(original.at(field).get<std::string>()));
    }
    changes.emplace_back("opslimit", original.at("opslimit").get<std::uint64_t>() + 1);
    changes.emplace_back("memlimit", original.at("memlimit").get<std::uint64_t>() * 2);
    changes.emplace_back("version", 2);
    changes.emplace_back("format", "mc-other");
    changes.emplace_back("algorithm", "ed448");
    changes.emplace_back("kdf", "scrypt");
    changes.emplace_back("cipher", "aes256gcm");
    changes.emplace_back("salt", "zz");

    for (const auto& [field, value] : changes) {
        json altered = original;
        altered[field] = value;
        write_text(keys.secret_key_file, altered.dump());
        FixedPasswords passwords{password};
        EXPECT_THROW(mc::pack::sign(keys.secret_key_file, pack_, signature(), passwords), ToolError) << field;
        EXPECT_FALSE(fs::exists(signature())) << field;
    }

    for (const std::string_view text : {"", "not json", "[]", "{}"}) {
        write_text(keys.secret_key_file, text);
        FixedPasswords passwords{password};
        EXPECT_THROW(mc::pack::sign(keys.secret_key_file, pack_, signature(), passwords), ToolError) << text;
        EXPECT_EQ(passwords.asked(), 0) << "a broken file fails before asking for the password";
    }
}

TEST_F(PackTool, OpenRejectsLimitsOutOfRange) {
    const auto keys = make_key();
    json file = json::parse(read_text(keys.secret_key_file));
    file["memlimit"] = std::uint64_t{crypto_pwhash_argon2id_MEMLIMIT_SENSITIVE} * 2;
    EXPECT_THROW((void)mc::pack::stored_limits(file.dump()), KeyFileError);
    file["memlimit"] = low_limits.memlimit;
    file["opslimit"] = 0;
    EXPECT_THROW((void)mc::pack::open_secret_key(file.dump(), as_bytes(password)), KeyFileError);
}

TEST_F(PackTool, VerifyRejectsAnAlteredPackSignatureOrKey) {
    const auto keys = make_key(0x11);
    FixedPasswords passwords{password};
    (void)mc::pack::sign(keys.secret_key_file, pack_, signature(), passwords);
    ASSERT_TRUE(mc::pack::verify(keys.public_key_file, pack_, signature()));

    const std::string pack = read_text(pack_);
    write_text(pack_, pack + " ");
    EXPECT_FALSE(mc::pack::verify(keys.public_key_file, pack_, signature()));
    write_text(pack_, pack);

    std::string detached = read_text(signature());
    detached[5] = static_cast<char>(detached[5] ^ 0x01);
    write_text(signature(), detached);
    EXPECT_FALSE(mc::pack::verify(keys.public_key_file, pack_, signature()));
    write_text(signature(), detached.substr(0, 63));
    EXPECT_FALSE(mc::pack::verify(keys.public_key_file, pack_, signature()));

    FixedPasswords again{password};
    (void)mc::pack::sign(keys.secret_key_file, pack_, signature(), again);
    const auto other = make_key(0x22, "other");
    EXPECT_FALSE(mc::pack::verify(other.public_key_file, pack_, signature()));

    write_text(dir_ / "bad.pub", "not a key");
    EXPECT_THROW((void)mc::pack::verify(dir_ / "bad.pub", pack_, signature()), ToolError);
    EXPECT_THROW((void)mc::pack::verify(keys.public_key_file, dir_ / "missing.json", signature()), ToolError);
}

int run(std::initializer_list<std::string_view> args, mc::pack::PasswordSource& passwords, std::string& out,
        std::string& err) {
    const std::vector<std::string_view> list(args);
    std::ostringstream out_stream;
    std::ostringstream err_stream;
    const int status = mc::pack::run(list, passwords, out_stream, err_stream);
    out = out_stream.str();
    err = err_stream.str();
    return status;
}

TEST_F(PackTool, CommandLine) {
    const auto keys = make_key(0x11);
    const std::string key = keys.secret_key_file.string();
    const std::string pub = keys.public_key_file.string();
    const std::string pack = pack_.string();
    const std::string sig = (dir_ / "out.sig").string();
    std::string out;
    std::string err;

    FixedPasswords one{password};
    EXPECT_EQ(run({"sign", key, pack, "--out", sig}, one, out, err), 0) << err;
    EXPECT_NE(out.find(keys.public_key_hex), std::string::npos);
    EXPECT_EQ(run({"verify", pub, pack, sig}, one, out, err), 0);
    EXPECT_EQ(out, "Valid signature\n");

    // By default the signature goes next to the pack, as PACK.sig.
    FixedPasswords two{password};
    EXPECT_EQ(run({"sign", key, pack}, two, out, err), 0) << err;
    EXPECT_TRUE(fs::exists(signature()));
    EXPECT_EQ(run({"verify", pub, pack}, two, out, err), 0);

    write_text(pack_, "changed");
    EXPECT_EQ(run({"verify", pub, pack}, two, out, err), 1);
    EXPECT_EQ(out, "Invalid signature\n");

    FixedPasswords wrong{"wrong"};
    EXPECT_EQ(run({"sign", key, pack}, wrong, out, err), 1);
    EXPECT_NE(err.find("wrong password"), std::string::npos);

    // keygen refuses to overwrite before it asks for a password or runs Argon2id.
    FixedPasswords none{};
    EXPECT_EQ(run({"keygen", "dev", "--dir", (dir_ / "keys").string()}, none, out, err), 1);
    EXPECT_NE(err.find("never overwrites"), std::string::npos);
    EXPECT_EQ(none.asked(), 0);

    for (const auto& args : std::vector<std::vector<std::string_view>>{{},
                                                                       {"unknown"},
                                                                       {"keygen"},
                                                                       {"keygen", "a", "b"},
                                                                       {"keygen", "a", "--dir"},
                                                                       {"keygen", "a", "--password", "x"},
                                                                       {"sign", key},
                                                                       {"verify", pub},
                                                                       {"verify", pub, pack, sig, sig}}) {
        std::ostringstream out_stream;
        std::ostringstream err_stream;
        EXPECT_EQ(mc::pack::run(args, none, out_stream, err_stream), 2);
        EXPECT_NE(err_stream.str().find("Usage:"), std::string::npos);
    }
    EXPECT_EQ(run({"help"}, none, out, err), 0);
    EXPECT_NE(out.find("never from arguments or the environment"), std::string::npos);
}

TEST(PackToolSecureBuffer, ShrinksAndMoves) {
    SecureBuffer buffer(8);
    for (std::size_t i = 0; i < buffer.size(); ++i) {
        buffer.data()[i] = static_cast<std::uint8_t>(i + 1);
    }
    buffer.shrink(3);
    EXPECT_EQ(buffer.size(), 3U);
    SecureBuffer moved(std::move(buffer));
    EXPECT_EQ(moved.size(), 3U);
    EXPECT_EQ(moved.data()[2], 3);
    EXPECT_TRUE(mc::pack::same_secret(moved.bytes(), moved.bytes()));
    SecureBuffer other(3);
    EXPECT_FALSE(mc::pack::same_secret(moved.bytes(), other.bytes()));
    other = std::move(moved);
    EXPECT_EQ(other.data()[0], 1);
    EXPECT_FALSE(mc::pack::same_secret(other.bytes(), SecureBuffer(2).bytes()));
}

}  // namespace
