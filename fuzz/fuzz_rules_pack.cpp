// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0
//
// Paquetes de reglas arbitrarios: el núcleo debe aceptarlos o rechazarlos sin fallar.

#include "mc/mc_api.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    mc_session* session = nullptr;
    std::uint8_t* error = nullptr;
    std::size_t error_len = 0;
    const mc_status status = mc_session_create(data, size, &session, &error, &error_len);
    // Con éxito hay sesión y no hay error; con un paquete malo, al revés.
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
