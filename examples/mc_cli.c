/*
 * SPDX-FileCopyrightText: 2026 Daniel Martín Millet
 * SPDX-License-Identifier: Apache-2.0
 *
 * mc_cli: a small command-line client to try the core by hand. It only uses the C API,
 * the same way the apps will (ADR-11).
 *
 * Usage:
 *   mc_cli PACK [COMMAND]
 *
 * It loads the rules pack PACK into a session, runs the JSON command read from the file
 * COMMAND (or from standard input when COMMAND is missing or "-") and prints the JSON reply.
 * Exit status: 0 when the core answers MC_OK, 1 when it rejects the pack or the command,
 * and 2 for usage or file errors.
 */
#include "mc/mc_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Larger than any pack or command the core accepts (16 MiB and 1 MiB). */
#define MAX_INPUT_BYTES ((size_t)32 * 1024 * 1024)

/* Reads the whole stream into a new buffer. Returns NULL on a read error or if it is too large. */
static uint8_t* read_all(FILE* stream, size_t* size) {
    size_t capacity = 4096;
    size_t length = 0;
    uint8_t* buffer = malloc(capacity);
    if (buffer == NULL) {
        return NULL;
    }
    for (;;) {
        if (length == capacity) {
            if (capacity >= MAX_INPUT_BYTES) {
                free(buffer);
                return NULL;
            }
            capacity *= 2;
            uint8_t* larger = realloc(buffer, capacity);
            if (larger == NULL) {
                free(buffer);
                return NULL;
            }
            buffer = larger;
        }
        const size_t chunk = fread(buffer + length, 1, capacity - length, stream);
        if (chunk == 0) {
            break;
        }
        length += chunk;
    }
    if (ferror(stream)) {
        free(buffer);
        return NULL;
    }
    *size = length;
    return buffer;
}

static uint8_t* read_file(const char* path, size_t* size) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    uint8_t* content = read_all(file, size);
    fclose(file);
    return content;
}

static void print_buffer(const uint8_t* buffer, size_t size) {
    fwrite(buffer, 1, size, stdout);
    putchar('\n');
}

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        fputs(
            "Usage: mc_cli PACK [COMMAND]\n"
            "Runs a JSON command, read from the COMMAND file or from standard input,\n"
            "on a session with the rules pack PACK, and prints the JSON reply.\n",
            stderr);
        return 2;
    }

    size_t pack_len = 0;
    uint8_t* pack = read_file(argv[1], &pack_len);
    if (pack == NULL) {
        fprintf(stderr, "mc_cli: cannot read the pack %s\n", argv[1]);
        return 2;
    }

    const int from_stdin = argc == 2 || strcmp(argv[2], "-") == 0;
    size_t cmd_len = 0;
    uint8_t* cmd = from_stdin ? read_all(stdin, &cmd_len) : read_file(argv[2], &cmd_len);
    if (cmd == NULL) {
        fprintf(stderr, "mc_cli: cannot read the command from %s\n", from_stdin ? "standard input" : argv[2]);
        free(pack);
        return 2;
    }

    mc_session* session = NULL;
    uint8_t* pack_error = NULL;
    size_t pack_error_len = 0;
    const mc_status created = mc_session_create(pack, pack_len, &session, &pack_error, &pack_error_len);
    free(pack);
    if (created != MC_OK) {
        fprintf(stderr, "mc_cli: the core rejected the pack (status %d)\n", (int)created);
        if (pack_error != NULL) {
            print_buffer(pack_error, pack_error_len);
        }
        mc_buffer_free(pack_error);
        free(cmd);
        return 1;
    }

    uint8_t* reply = NULL;
    size_t reply_len = 0;
    const mc_status status = mc_session_dispatch(session, cmd, cmd_len, &reply, &reply_len);
    free(cmd);
    if (reply != NULL) {
        print_buffer(reply, reply_len);
    }
    mc_buffer_free(reply);
    mc_session_destroy(session);
    return status == MC_OK ? 0 : 1;
}
