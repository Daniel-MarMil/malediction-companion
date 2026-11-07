// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0
//
// Comandos arbitrarios sobre una sesión con el paquete de prueba: toda respuesta debe ser
// un JSON terminado en un byte nulo y el núcleo no debe fallar.

#include "mc/mc_api.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

namespace {

mc_session* test_session() {
    static mc_session* session = [] {
        std::ifstream file(MC_TEST_PACK_PATH, std::ios::binary);
        const std::string pack{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        mc_session* created = nullptr;
        if (mc_session_create(reinterpret_cast<const std::uint8_t*>(pack.data()), pack.size(), &created, nullptr,
                              nullptr) != MC_OK) {
            std::abort();
        }
        return created;
    }();
    return session;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    std::uint8_t* out = nullptr;
    std::size_t out_len = 0;
    const mc_status status = mc_session_dispatch(test_session(), data, size, &out, &out_len);
    if (status != MC_OK && status != MC_ERR_BAD_COMMAND) {
        std::abort();
    }
    if (out == nullptr || out[out_len] != 0 || out_len == 0 || out[0] != '{') {
        std::abort();
    }
    mc_buffer_free(out);
    return 0;
}
