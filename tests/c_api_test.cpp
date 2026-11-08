// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0
//
// Tests for the C API as Kotlin (through JNI) and Swift use it (ADR-11, RNF-21).

#include "mc/mc_api.h"
#include "test_support.hpp"

#include <gtest/gtest.h>
#include <sodium.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

extern "C" int mc_c_header_check(void);

namespace {

using nlohmann::json;

/// A buffer returned by the core; it is freed only with mc_buffer_free.
struct Buffer {
    Buffer() = default;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    ~Buffer() { mc_buffer_free(data); }

    [[nodiscard]] std::string_view text() const {
        return data == nullptr ? std::string_view{} : std::string_view(reinterpret_cast<const char*>(data), size);
    }
    [[nodiscard]] json parsed() const { return json::parse(text()); }

    uint8_t* data = nullptr;
    size_t size = 0;
};

const uint8_t* bytes(std::string_view text) {
    return reinterpret_cast<const uint8_t*>(text.data());
}

/// A session with the test pack; it is destroyed automatically.
class Session {
public:
    Session() {
        const std::string& pack = mc::test::test_pack_json();
        status_ = mc_session_create(bytes(pack), pack.size(), &session_, &error_.data, &error_.size);
    }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    ~Session() { mc_session_destroy(session_); }

    [[nodiscard]] mc_status status() const { return status_; }
    [[nodiscard]] mc_session* get() const { return session_; }

    mc_status dispatch(std::string_view command, Buffer& out) const {
        return mc_session_dispatch(session_, bytes(command), command.size(), &out.data, &out.size);
    }

private:
    mc_session* session_ = nullptr;
    Buffer error_;
    mc_status status_ = MC_ERR_INTERNAL;
};

json deck_json(const mc::core::Deck& deck) {
    json result = json::object();
    result["seeker"] = deck.seeker_id;
    result["legacy"] = deck.legacy_id;
    result["terrain"] = deck.terrain_id;
    result["main"] = json::array();
    for (const auto& entry : deck.main) {
        result["main"].push_back({{"card", entry.card_id}, {"copies", entry.copies}});
    }
    return result;
}

std::string validate_command(const json& deck) {
    json command = json::object();
    command["cmd"] = "validate_deck";
    command["deck"] = deck;
    return command.dump();
}

TEST(CApi, ReportsItsVersion) {
    EXPECT_STREQ(mc_version(), MC_EXPECTED_VERSION);
}

TEST(CApi, HeaderWorksFromC) {
    EXPECT_EQ(mc_c_header_check(), MC_ERR_BAD_PACK);
}

TEST(CApi, CreatesASessionWithTheTestPack) {
    const Session session;
    ASSERT_EQ(session.status(), MC_OK);
    ASSERT_NE(session.get(), nullptr);

    Buffer out;
    ASSERT_EQ(session.dispatch(R"({"cmd":"pack_info"})", out), MC_OK);
    const json reply = out.parsed();
    EXPECT_EQ(reply["ok"], true);
    EXPECT_EQ(reply["pack"]["id"], "mc-test");
    EXPECT_EQ(reply["pack"]["cards"], 35);
}

TEST(CApi, ReturnsNulTerminatedBuffers) {
    const Session session;
    Buffer out;
    ASSERT_EQ(session.dispatch(R"({"cmd":"pack_info"})", out), MC_OK);
    ASSERT_NE(out.data, nullptr);
    EXPECT_EQ(out.data[out.size], 0);
    EXPECT_EQ(std::strlen(reinterpret_cast<const char*>(out.data)), out.size);
}

TEST(CApi, ValidatesADeck) {
    const Session session;
    Buffer out;
    ASSERT_EQ(session.dispatch(validate_command(deck_json(mc::test::valid_ash_deck())), out), MC_OK);
    json reply = out.parsed();
    EXPECT_EQ(reply["ok"], true);
    EXPECT_EQ(reply["valid"], true);
    EXPECT_TRUE(reply["issues"].empty());

    mc::core::Deck deck = mc::test::valid_ash_deck();
    deck.main.front().copies = 4;
    deck.terrain_id.clear();
    Buffer invalid;
    ASSERT_EQ(session.dispatch(validate_command(deck_json(deck)), invalid), MC_OK);
    reply = invalid.parsed();
    EXPECT_EQ(reply["valid"], false);
    std::vector<std::string> rules;
    for (const auto& issue : reply["issues"]) {
        rules.push_back(issue["rule"].get<std::string>());
        EXPECT_TRUE(issue["detail"].is_string());
    }
    EXPECT_EQ(rules, (std::vector<std::string>{"terrain_missing", "too_many_copies"}));
    EXPECT_EQ(reply["issues"][1]["card"], "T-ASH-U01");
    EXPECT_FALSE(reply["issues"][0].contains("card"));
}

TEST(CApi, AnswersBadCommandsWithAnError) {
    const Session session;
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"not json", "invalid_json"},
        {"", "invalid_json"},
        {"\"\xff\"", "invalid_json"},
        {"[]", "missing_cmd"},
        {R"({"cmd":42})", "missing_cmd"},
        {R"({"cmd":"delete_everything"})", "unknown_command"},
        {R"({"cmd":"validate_deck"})", "bad_deck"},
        {R"({"cmd":"validate_deck","deck":[]})", "bad_deck"},
        {R"({"cmd":"validate_deck","deck":{"seeker":1}})", "bad_deck"},
        {R"({"cmd":"validate_deck","deck":{"main":{}}})", "bad_deck"},
        {R"({"cmd":"validate_deck","deck":{"main":[1]}})", "bad_deck"},
        {R"({"cmd":"validate_deck","deck":{"main":[{"copies":1}]}})", "bad_deck"},
        {R"({"cmd":"validate_deck","deck":{"main":[{"card":"T-ASH-U01","copies":1.5}]}})", "bad_deck"},
        {R"({"cmd":"validate_deck","deck":{"main":[{"card":"T-ASH-U01","copies":99999999999}]}})", "bad_deck"},
    };
    for (const auto& [command, code] : cases) {
        Buffer out;
        EXPECT_EQ(session.dispatch(command, out), MC_ERR_BAD_COMMAND) << command;
        const json reply = out.parsed();
        EXPECT_EQ(reply["ok"], false) << command;
        EXPECT_EQ(reply["error"], code) << command;
        EXPECT_TRUE(reply["detail"].is_string()) << command;
    }
}

TEST(CApi, AcceptsNullSlotsAsMissing) {
    const Session session;
    Buffer out;
    ASSERT_EQ(session.dispatch(R"({"cmd":"validate_deck","deck":{"seeker":null,"main":null}})", out), MC_OK);
    const json reply = out.parsed();
    EXPECT_EQ(reply["valid"], false);
    EXPECT_EQ(reply["issues"][0]["rule"], "seeker_missing");
}

TEST(CApi, RejectsDecksWithTooManyLines) {
    const Session session;
    json deck = json::object();
    deck["main"] = json::array();
    for (int i = 0; i < 501; ++i) {
        deck["main"].push_back({{"card", "T-ASH-U01"}, {"copies", 1}});
    }
    Buffer out;
    EXPECT_EQ(session.dispatch(validate_command(deck), out), MC_ERR_BAD_COMMAND);
    EXPECT_EQ(out.parsed()["error"], "bad_deck");
}

TEST(CApi, RejectsOversizedCommands) {
    const Session session;
    const std::string command(std::size_t{1024} * 1024 + 1, ' ');
    Buffer out;
    EXPECT_EQ(session.dispatch(command, out), MC_ERR_BAD_COMMAND);
    EXPECT_EQ(out.parsed()["error"], "too_large");
}

TEST(CApi, ExplainsWhyAPackIsRejected) {
    const std::string pack = R"({"format":"other"})";
    mc_session* session = nullptr;
    Buffer error;
    EXPECT_EQ(mc_session_create(bytes(pack), pack.size(), &session, &error.data, &error.size), MC_ERR_BAD_PACK);
    EXPECT_EQ(session, nullptr);
    const json reply = error.parsed();
    EXPECT_EQ(reply["ok"], false);
    EXPECT_EQ(reply["error"], "bad_pack");
    EXPECT_NE(reply["detail"].get<std::string>().find("format"), std::string::npos);

    // It also works without an error buffer.
    EXPECT_EQ(mc_session_create(bytes(pack), pack.size(), &session, nullptr, nullptr), MC_ERR_BAD_PACK);
}

TEST(CApi, RejectsOversizedPacks) {
    const std::string pack(std::size_t{16} * 1024 * 1024 + 1, ' ');
    mc_session* session = nullptr;
    Buffer error;
    EXPECT_EQ(mc_session_create(bytes(pack), pack.size(), &session, &error.data, &error.size), MC_ERR_BAD_PACK);
    EXPECT_EQ(error.parsed()["error"], "too_large");
}

TEST(CApi, ChecksItsArguments) {
    const std::string pack = mc::test::test_pack_json();
    mc_session* session = nullptr;
    uint8_t* error = nullptr;
    size_t error_len = 0;
    EXPECT_EQ(mc_session_create(bytes(pack), pack.size(), nullptr, nullptr, nullptr), MC_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(mc_session_create(nullptr, 10, &session, nullptr, nullptr), MC_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(mc_session_create(bytes(pack), pack.size(), &session, &error, nullptr), MC_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(mc_session_create(bytes(pack), pack.size(), &session, nullptr, &error_len), MC_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(session, nullptr);

    const Session valid;
    Buffer out;
    EXPECT_EQ(mc_session_dispatch(nullptr, bytes("{}"), 2, &out.data, &out.size), MC_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(mc_session_dispatch(valid.get(), nullptr, 2, &out.data, &out.size), MC_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(mc_session_dispatch(valid.get(), bytes("{}"), 2, nullptr, &out.size), MC_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(mc_session_dispatch(valid.get(), bytes("{}"), 2, &out.data, nullptr), MC_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(out.data, nullptr);

    // Freeing NULL does nothing.
    mc_buffer_free(nullptr);
    mc_session_destroy(nullptr);
}

TEST(CApi, VerifiesPackSignatures) {
    ASSERT_GE(sodium_init(), 0);
    std::array<uint8_t, crypto_sign_SEEDBYTES> seed{};
    seed.fill(3);
    std::array<uint8_t, crypto_sign_PUBLICKEYBYTES> public_key{};
    std::array<uint8_t, crypto_sign_SECRETKEYBYTES> secret_key{};
    crypto_sign_seed_keypair(public_key.data(), secret_key.data(), seed.data());

    const std::string& pack = mc::test::test_pack_json();
    std::array<uint8_t, crypto_sign_BYTES> signature{};
    crypto_sign_detached(signature.data(), nullptr, bytes(pack), pack.size(), secret_key.data());

    EXPECT_EQ(mc_pack_verify(bytes(pack), pack.size(), signature.data(), signature.size(), public_key.data(),
                             public_key.size()),
              MC_OK);
    EXPECT_EQ(mc_pack_verify(bytes(pack), pack.size() - 1, signature.data(), signature.size(), public_key.data(),
                             public_key.size()),
              MC_ERR_BAD_PACK);
    EXPECT_EQ(mc_pack_verify(bytes(pack), pack.size(), signature.data(), signature.size() - 1, public_key.data(),
                             public_key.size()),
              MC_ERR_BAD_PACK);
    EXPECT_EQ(mc_pack_verify(bytes(pack), pack.size(), nullptr, 0, public_key.data(), public_key.size()),
              MC_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(mc_pack_verify(nullptr, 1, signature.data(), signature.size(), public_key.data(), public_key.size()),
              MC_ERR_INVALID_ARGUMENT);
}

}  // namespace
