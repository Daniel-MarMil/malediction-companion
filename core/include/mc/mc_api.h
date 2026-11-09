/*
 * SPDX-FileCopyrightText: 2026 Daniel Martín Millet
 * SPDX-License-Identifier: Apache-2.0
 *
 * C API of the core (ADR-11): the only boundary between the C++ core and the apps.
 * Kotlin calls it through a thin JNI layer and Swift imports it directly.
 *
 * Boundary rules:
 *  - No function throws exceptions; every function returns an mc_status.
 *  - Commands and responses are UTF-8 JSON, passed as bytes.
 *  - A session is not thread-safe: use it from a single thread or actor.
 *  - The core creates no threads and never calls back into the app.
 *  - Every buffer the core returns is freed with mc_buffer_free.
 */
#ifndef MC_API_H
#define MC_API_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(MC_SHARED)
#  if defined(MC_BUILDING_LIBRARY)
#    define MC_API __declspec(dllexport)
#  else
#    define MC_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define MC_API __attribute__((visibility("default")))
#else
#  define MC_API
#endif

#ifdef __cplusplus
#  define MC_NOEXCEPT noexcept
extern "C" {
#else
#  define MC_NOEXCEPT
#endif

/* This is a C header: typedefs and unscoped enums without an underlying type are right here. */
/* NOLINTBEGIN(modernize-use-using, performance-enum-size, cppcoreguidelines-use-enum-class) */

/** Opaque session: a loaded rules pack and its state. */
typedef struct mc_session mc_session;

/** Result of every call. */
typedef enum mc_status {
    MC_OK = 0,
    MC_ERR_INVALID_ARGUMENT = 1, /**< Null pointer or an impossible combination of arguments. */
    MC_ERR_BAD_PACK = 2,         /**< The rules pack or its signature is not valid. */
    MC_ERR_BAD_COMMAND = 3,      /**< The command is not valid JSON, does not exist or carries unusable data. */
    MC_ERR_INTERNAL = 4          /**< Unexpected core error, for example out of memory. */
} mc_status;

/* NOLINTEND(modernize-use-using, performance-enum-size, cppcoreguidelines-use-enum-class) */

/** Core version, for example "0.1.0". It is a static string: do not free it. */
MC_API const char* mc_version(void) MC_NOEXCEPT;

/**
 * Checks the detached Ed25519 signature of a pack (ADR-06).
 * Returns MC_OK if the signature is valid and MC_ERR_BAD_PACK if it is not.
 */
MC_API mc_status mc_pack_verify(const uint8_t* pack, size_t pack_len, const uint8_t* signature, size_t signature_len,
                                const uint8_t* public_key, size_t public_key_len) MC_NOEXCEPT;

/**
 * Checks the detached Ed25519 signature of a pack against the public keys compiled into the
 * core (ADR-06): the release keys and, only in Debug builds with MC_TRUST_DEV_KEY, the
 * development key. Returns MC_OK if one of them signed the pack and MC_ERR_BAD_PACK if none did.
 */
MC_API mc_status mc_pack_verify_trusted(const uint8_t* pack, size_t pack_len, const uint8_t* signature,
                                        size_t signature_len) MC_NOEXCEPT;

/**
 * Creates a session from a JSON rules pack.
 * If the pack is not valid it returns MC_ERR_BAD_PACK and, when out_error is not NULL,
 * stores in *out_error a JSON object with the reason, to be freed with mc_buffer_free.
 * out_error and out_error_len are optional, but must be given together.
 */
MC_API mc_status mc_session_create(const uint8_t* pack, size_t pack_len, mc_session** out_session, uint8_t** out_error,
                                   size_t* out_error_len) MC_NOEXCEPT;

/**
 * Runs a JSON command, for example {"cmd":"validate_deck","deck":{...}}.
 * With MC_OK or MC_ERR_BAD_COMMAND, *out receives the JSON response (UTF-8, terminated by
 * a null byte that *out_len does not count), to be freed with mc_buffer_free.
 */
MC_API mc_status mc_session_dispatch(mc_session* session, const uint8_t* cmd, size_t cmd_len, uint8_t** out,
                                     size_t* out_len) MC_NOEXCEPT;

/** Frees a buffer returned by the core. Accepts NULL. */
MC_API void mc_buffer_free(uint8_t* buffer) MC_NOEXCEPT;

/** Destroys a session. Accepts NULL. */
MC_API void mc_session_destroy(mc_session* session) MC_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif /* MC_API_H */
