/*
 * SPDX-FileCopyrightText: 2026 Daniel Martín Millet
 * SPDX-License-Identifier: Apache-2.0
 *
 * Comprueba que mc_api.h es C11 válido y que sus funciones se enlazan desde C.
 */
#include "mc/mc_api.h"

int mc_c_header_check(void);

int mc_c_header_check(void) {
    mc_session* session = NULL;
    const mc_status status = mc_session_create(NULL, 0, &session, NULL, NULL);
    mc_session_destroy(session);
    return (int)status;
}
