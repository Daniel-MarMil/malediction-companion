// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0
//
// Implementation of the C API (ADR-11). No exception crosses this boundary.

#include "mc/mc_api.h"

#include "json_io.hpp"
#include "mc/core/deck_text.hpp"
#include "mc/core/deck_validator.hpp"
#include "mc/core/rules_pack.hpp"
#include "mc/core/signature.hpp"
#include "mc/core/trusted_keys.hpp"

#include <nlohmann/json.hpp>

#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#ifndef MC_VERSION_STRING
#  error "MC_VERSION_STRING must be defined when building the core"
#endif

struct mc_session {
    explicit mc_session(mc::core::RulesPack loaded) : pack(std::move(loaded)) {}
    mc::core::RulesPack pack;
};

namespace {

using nlohmann::json;
namespace core = mc::core;

// Defensive limits for data that may come from the network.
constexpr std::size_t max_pack_bytes = std::size_t{16} * 1024 * 1024;
constexpr std::size_t max_command_bytes = std::size_t{1} * 1024 * 1024;

struct Reply {
    mc_status status;
    std::string body;
};

std::string error_body(std::string_view code, std::string_view detail) {
    json body = json::object();
    body["ok"] = false;
    body["error"] = std::string(code);
    body["detail"] = std::string(detail);
    return core::detail::to_text(body);
}

Reply bad_command(std::string_view code, std::string_view detail) {
    return {.status = MC_ERR_BAD_COMMAND, .body = error_body(code, detail)};
}

/// Copies the text into a new null-terminated buffer. Returns false when out of memory.
bool copy_out(std::string_view text, std::uint8_t** out, std::size_t* out_len) noexcept {
    auto* buffer = new (std::nothrow) std::uint8_t[text.size() + 1];
    if (buffer == nullptr) {
        return false;
    }
    if (!text.empty()) {
        std::memcpy(buffer, text.data(), text.size());
    }
    buffer[text.size()] = 0;
    *out = buffer;
    *out_len = text.size();
    return true;
}

std::string_view as_text(const std::uint8_t* bytes, std::size_t size) noexcept {
    if (size == 0) {
        return {};
    }
    return {reinterpret_cast<const char*>(bytes), size};
}

Reply pack_info(const mc_session& session) {
    json body = json::object();
    body["ok"] = true;
    body["pack"] = core::detail::pack_info_to_json(session.pack);
    return {.status = MC_OK, .body = core::detail::to_text(body)};
}

Reply validate_deck(const mc_session& session, const json& command) {
    const auto field = command.find("deck");
    if (field == command.end()) {
        return bad_command("bad_deck", "the deck field is missing");
    }
    core::Deck deck;
    try {
        deck = core::detail::deck_from_json(*field);
    } catch (const core::detail::DeckFormatError& error) {
        return bad_command("bad_deck", error.what());
    }
    const auto issues = core::validate_deck(session.pack, deck);
    json body = json::object();
    body["ok"] = true;
    body["valid"] = issues.empty();
    body["issues"] = core::detail::issues_to_json(issues);
    return {.status = MC_OK, .body = core::detail::to_text(body)};
}

Reply import_deck(const mc_session& session, const json& command) {
    const auto field = command.find("text");
    if (field == command.end() || !field->is_string()) {
        return bad_command("bad_text", "the text field must be a string");
    }
    const core::ImportedDeck imported = core::import_deck_text(session.pack, field->get_ref<const std::string&>());
    json body = json::object();
    body["ok"] = true;
    body["deck"] = core::detail::deck_to_json(imported.deck);
    body["errors"] = core::detail::deck_text_errors_to_json(imported.errors);
    if (imported.errors_truncated) {
        body["errors_truncated"] = true;
    }
    return {.status = MC_OK, .body = core::detail::to_text(body)};
}

Reply export_deck(const mc_session& session, const json& command) {
    const auto field = command.find("deck");
    if (field == command.end()) {
        return bad_command("bad_deck", "the deck field is missing");
    }
    core::Deck deck;
    try {
        deck = core::detail::deck_from_json(*field);
    } catch (const core::detail::DeckFormatError& error) {
        return bad_command("bad_deck", error.what());
    }
    std::string text;
    try {
        text = core::export_deck_text(session.pack, deck);
    } catch (const core::DeckExportError& error) {
        return bad_command(error.code(), error.what());
    }
    json body = json::object();
    body["ok"] = true;
    body["text"] = std::move(text);
    return {.status = MC_OK, .body = core::detail::to_text(body)};
}

Reply dispatch(const mc_session& session, std::string_view text) {
    if (text.size() > max_command_bytes) {
        return bad_command("too_large", "the command is larger than 1 MiB");
    }
    json command;
    try {
        command = json::parse(text.begin(), text.end());
    } catch (const json::exception& error) {
        return bad_command("invalid_json", error.what());
    }
    const auto name = command.is_object() ? command.find("cmd") : command.end();
    if (!command.is_object() || name == command.end() || !name->is_string()) {
        return bad_command("missing_cmd", "the command must be a JSON object with a cmd string");
    }
    const auto& cmd = name->get_ref<const std::string&>();
    if (cmd == "pack_info") {
        return pack_info(session);
    }
    if (cmd == "validate_deck") {
        return validate_deck(session, command);
    }
    if (cmd == "import_deck") {
        return import_deck(session, command);
    }
    if (cmd == "export_deck") {
        return export_deck(session, command);
    }
    return bad_command("unknown_command", "unknown command: " + cmd);
}

}  // namespace

extern "C" {

const char* mc_version(void) noexcept {
    return MC_VERSION_STRING;
}

mc_status mc_pack_verify(const uint8_t* pack, size_t pack_len, const uint8_t* signature, size_t signature_len,
                         const uint8_t* public_key, size_t public_key_len) noexcept {
    if ((pack == nullptr && pack_len != 0) || signature == nullptr || public_key == nullptr) {
        return MC_ERR_INVALID_ARGUMENT;
    }
    const bool valid = core::verify_signature(std::span<const std::uint8_t>(pack, pack_len),
                                              std::span<const std::uint8_t>(signature, signature_len),
                                              std::span<const std::uint8_t>(public_key, public_key_len));
    return valid ? MC_OK : MC_ERR_BAD_PACK;
}

mc_status mc_pack_verify_trusted(const uint8_t* pack, size_t pack_len, const uint8_t* signature,
                                 size_t signature_len) noexcept {
    if ((pack == nullptr && pack_len != 0) || signature == nullptr) {
        return MC_ERR_INVALID_ARGUMENT;
    }
    const bool valid = core::verify_trusted(std::span<const std::uint8_t>(pack, pack_len),
                                            std::span<const std::uint8_t>(signature, signature_len));
    return valid ? MC_OK : MC_ERR_BAD_PACK;
}

mc_status mc_session_create(const uint8_t* pack, size_t pack_len, mc_session** out_session, uint8_t** out_error,
                            size_t* out_error_len) noexcept {
    if ((out_error == nullptr) != (out_error_len == nullptr)) {
        return MC_ERR_INVALID_ARGUMENT;
    }
    if (out_error != nullptr) {
        *out_error = nullptr;
        *out_error_len = 0;
    }
    if (out_session == nullptr || (pack == nullptr && pack_len != 0)) {
        return MC_ERR_INVALID_ARGUMENT;
    }
    *out_session = nullptr;

    // Without memory for the detail, the error is still returned, only without text.
    const auto report = [&](std::string_view code, std::string_view detail) noexcept {
        if (out_error == nullptr) {
            return;
        }
        try {
            (void)copy_out(error_body(code, detail), out_error, out_error_len);
        } catch (...) {
            *out_error = nullptr;
            *out_error_len = 0;
        }
    };

    if (pack_len > max_pack_bytes) {
        report("too_large", "the pack is larger than 16 MiB");
        return MC_ERR_BAD_PACK;
    }
    try {
        auto session = std::make_unique<mc_session>(core::RulesPack::parse(as_text(pack, pack_len)));
        *out_session = session.release();
        return MC_OK;
    } catch (const core::PackError& error) {
        report("bad_pack", error.what());
        return MC_ERR_BAD_PACK;
    } catch (const std::bad_alloc&) {
        return MC_ERR_INTERNAL;
    } catch (const std::exception& error) {
        report("internal", error.what());
        return MC_ERR_INTERNAL;
    } catch (...) {
        report("internal", "unknown error");
        return MC_ERR_INTERNAL;
    }
}

mc_status mc_session_dispatch(mc_session* session, const uint8_t* cmd, size_t cmd_len, uint8_t** out,
                              size_t* out_len) noexcept {
    if (out == nullptr || out_len == nullptr) {
        return MC_ERR_INVALID_ARGUMENT;
    }
    *out = nullptr;
    *out_len = 0;
    if (session == nullptr || (cmd == nullptr && cmd_len != 0)) {
        return MC_ERR_INVALID_ARGUMENT;
    }
    try {
        const Reply reply = dispatch(*session, as_text(cmd, cmd_len));
        if (!copy_out(reply.body, out, out_len)) {
            return MC_ERR_INTERNAL;
        }
        return reply.status;
    } catch (...) {
        return MC_ERR_INTERNAL;
    }
}

// Like free(), it takes a non-const pointer: whoever frees the buffer owns it.
void mc_buffer_free(uint8_t* buffer) noexcept {  // NOLINT(readability-non-const-parameter)
    delete[] buffer;
}

void mc_session_destroy(mc_session* session) noexcept {
    delete session;
}

}  // extern "C"
