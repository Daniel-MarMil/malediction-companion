/*
 * SPDX-FileCopyrightText: 2026 Daniel Martín Millet
 * SPDX-License-Identifier: Apache-2.0
 *
 * API en C del núcleo (ADR-11): la única frontera entre el núcleo C++ y las apps.
 * Kotlin la llama a través de una capa JNI fina y Swift la importa directamente.
 *
 * Reglas de la frontera:
 *  - Ninguna función lanza excepciones; todas devuelven un mc_status.
 *  - Los comandos y las respuestas son JSON en UTF-8, pasados como bytes.
 *  - Una sesión no es segura entre hilos: úsala desde un solo hilo o actor.
 *  - El núcleo no crea hilos ni llama de vuelta a la app.
 *  - Todo búfer que devuelve el núcleo se libera con mc_buffer_free.
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

/* Es una cabecera C: typedef y enum sin tipo base son lo correcto aquí. */
/* NOLINTBEGIN(modernize-use-using, performance-enum-size) */

/** Sesión opaca: un paquete de reglas cargado y su estado. */
typedef struct mc_session mc_session;

/** Resultado de cada llamada. */
typedef enum mc_status {
    MC_OK = 0,
    MC_ERR_INVALID_ARGUMENT = 1, /**< Puntero nulo o combinación de argumentos imposible. */
    MC_ERR_BAD_PACK = 2,         /**< El paquete de reglas o su firma no son válidos. */
    MC_ERR_BAD_COMMAND = 3,      /**< El comando no es JSON válido, no existe o sus datos no sirven. */
    MC_ERR_INTERNAL = 4          /**< Error inesperado del núcleo, por ejemplo sin memoria. */
} mc_status;

/* NOLINTEND(modernize-use-using, performance-enum-size) */

/** Versión del núcleo, por ejemplo "0.1.0". Es una cadena estática: no se libera. */
MC_API const char* mc_version(void) MC_NOEXCEPT;

/**
 * Comprueba la firma Ed25519 separada de un paquete (ADR-06).
 * Devuelve MC_OK si la firma es válida y MC_ERR_BAD_PACK si no lo es.
 */
MC_API mc_status mc_pack_verify(const uint8_t* pack, size_t pack_len, const uint8_t* signature, size_t signature_len,
                                const uint8_t* public_key, size_t public_key_len) MC_NOEXCEPT;

/**
 * Crea una sesión con un paquete de reglas en JSON.
 * Si el paquete no es válido devuelve MC_ERR_BAD_PACK y, cuando out_error no es NULL,
 * deja en *out_error un JSON con el motivo, que hay que liberar con mc_buffer_free.
 * out_error y out_error_len son opcionales, pero van juntos.
 */
MC_API mc_status mc_session_create(const uint8_t* pack, size_t pack_len, mc_session** out_session, uint8_t** out_error,
                                   size_t* out_error_len) MC_NOEXCEPT;

/**
 * Ejecuta un comando JSON, por ejemplo {"cmd":"validate_deck","deck":{...}}.
 * Con MC_OK o MC_ERR_BAD_COMMAND, *out recibe la respuesta JSON (UTF-8, terminada en
 * un byte nulo que *out_len no cuenta) y hay que liberarla con mc_buffer_free.
 */
MC_API mc_status mc_session_dispatch(mc_session* session, const uint8_t* cmd, size_t cmd_len, uint8_t** out,
                                     size_t* out_len) MC_NOEXCEPT;

/** Libera un búfer devuelto por el núcleo. Admite NULL. */
MC_API void mc_buffer_free(uint8_t* buffer) MC_NOEXCEPT;

/** Destruye una sesión. Admite NULL. */
MC_API void mc_session_destroy(mc_session* session) MC_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif /* MC_API_H */
