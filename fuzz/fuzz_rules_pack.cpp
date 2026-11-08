// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0
//
// Arbitrary rules packs: the core must accept or reject them without crashing.

#include "mc/mc_api.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    mc_session* session = nullptr;
    std::uint8_t* error = nullptr;
    std::size_t error_len = 0;
    const mc_status status = mc_session_create(data, size, &session, &error, &error_len);
    // On success there is a session and no error; with a bad pack, the other way round.
    if ((status == MC_OK) != (session != nullptr)) {
        std::abort();
    }
    if (status == MC_ERR_BAD_PACK && error == nullptr) {
        std::abort();
    }
    mc_buffer_free(error);
    mc_session_destroy(session);
    return 0;
}
