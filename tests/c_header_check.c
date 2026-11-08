/*
 * SPDX-FileCopyrightText: 2026 Daniel Martín Millet
 * SPDX-License-Identifier: Apache-2.0
 *
 * Checks that mc_api.h is valid C11 and that its functions link from C.
 */
#include "mc/mc_api.h"

int mc_c_header_check(void);

int mc_c_header_check(void) {
    mc_session* session = NULL;
    const mc_status status = mc_session_create(NULL, 0, &session, NULL, NULL);
    mc_session_destroy(session);
    return (int)status;
}
