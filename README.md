<!--
SPDX-FileCopyrightText: 2026 Daniel Martín Millet
SPDX-License-Identifier: Apache-2.0
-->

# App Malediction

Compañero digital para partidas de [Malediction](https://malediction.gg), el juego de cartas con
miniaturas de Loot Studios: mazos que cumplen el reglamento, contadores de eco y maestría
sincronizados entre los móviles y un registro de partidas. Toma como referencia el recorrido de
la app de Warhammer 40,000, adaptado a un juego de cartas.

**Proyecto no oficial.** No está afiliado a Loot Studios ni cuenta con su respaldo.

## Estado

Fase 0: el núcleo compartido en C++20.

- Paquetes de reglas versionados (formato `mc-rules-pack` 1) con firma Ed25519 separada (ADR-06).
- Validación de mazos (RF-06): Seeker, Legacy y terreno; 30 a 50 cartas; sigilos; copias por
  rango; mazo auxiliar de 7. Los límites vienen del paquete, no del código.
- API en C para las apps (ADR-11).
- Pruebas con GoogleTest y RapidCheck, sanitizers, cobertura y fuzzing con libFuzzer.

Después llegan la app Android (Kotlin con Jetpack Compose) y la app iOS (Swift con SwiftUI). La
app iOS necesita un Mac con Xcode; mientras no haya uno, el trabajo sigue en Android.

## Contenido oficial

El proyecto se desarrolla con las cartas, imágenes, tipografías, colores e iconos oficiales de
Malediction como si tuviera licencia (ADR-09), pero ese contenido **no está en el repositorio**:
vive en `content/official/`, que git ignora (ADR-12). Las condiciones de malediction.gg prohíben
reproducirlo o distribuirlo sin permiso expreso, también sin ánimo de lucro, así que antes de
publicar cualquier versión hay que pedir licencia a Loot Studios. Más detalles en
[`content/official/README.md`](content/official/README.md).

Las pruebas usan un paquete de cartas ficticias: [`content/test/rules-pack.test.json`](content/test/rules-pack.test.json).

## Estructura

```text
core/                 Núcleo C++20 (sin interfaz ni E/S)
  include/mc/mc_api.h   API en C, la única frontera con las apps
  include/mc/core/      Tipos del dominio: cartas, paquete de reglas, mazo, validación
  src/                  Implementación
tests/                Pruebas (GoogleTest, RapidCheck) y comprobación de la cabecera en C11
fuzz/                 Objetivos de libFuzzer y corpus inicial
content/test/         Paquete de reglas ficticio para las pruebas
content/official/     Contenido oficial privado (ignorado por git)
docs/                 Decisiones de arquitectura (adr/) y guía del entorno local
tools/ci/             Tareas de la integración continua, también para uso local
tools/dev/            Preparación del entorno local
.woodpecker/          Flujos de Woodpecker CI
```

## Compilar

Para preparar en Windows el entorno completo (WSL 2, Android Studio y, si se quiere, MSVC), sigue
[`docs/entorno-local.md`](docs/entorno-local.md).

Hace falta CMake 3.25 o posterior, Ninja y un compilador C++20 (GCC 13, Clang 18 o el MSVC de
Visual Studio 2022 o 2026). Las dependencias salen de [vcpkg](https://vcpkg.io) en modo
manifiesto, con la versión fijada en `vcpkg.json` (ADR-10); la variable `VCPKG_ROOT` debe apuntar
a vcpkg. `tools/ci/bootstrap-vcpkg.sh` lo descarga en esa versión.

```sh
cmake --preset dev            # Debug con pruebas; «release» para Release
cmake --build --preset dev
ctest --preset dev
```

En Windows, abre la carpeta con Visual Studio 2022 (17.5 o posterior) o 2026 con la integración
de `CMakePresets.json` activada, o usa la consola de desarrollador. Los presets `asan`, `tsan`,
`coverage` y `fuzz` están pensados para Linux o WSL 2; `tools/ci/ci.sh` los ejecuta igual que la
integración continua:

```sh
tools/ci/ci.sh asan        # AddressSanitizer y UBSan
tools/ci/ci.sh tsan        # ThreadSanitizer
tools/ci/ci.sh coverage    # cobertura; falla por debajo del 90 %
tools/ci/ci.sh fuzz        # Clang y libFuzzer
tools/ci/ci.sh all         # todo lo que hace la integración continua
```

Fuera de Windows, vcpkg compila libsodium con autotools: instala `autoconf`, `autoconf-archive`,
`automake` y `libtool`.

### Sin vcpkg

En Linux se puede compilar con las bibliotecas del sistema. En Ubuntu 24.04:

```sh
sudo apt install libsodium-dev nlohmann-json3-dev libgtest-dev librapidcheck-dev
cmake --preset system && cmake --build --preset system && ctest --preset system
```

### Android

Con el NDK r28 o posterior (la integración continua usa el r30, la versión LTS actual) y la
variable `ANDROID_NDK_HOME`:

```sh
cmake --preset android-arm64 && cmake --build --preset android-arm64    # móviles
cmake --preset android-x86_64 && cmake --build --preset android-x86_64  # emulador
python3 tools/ci/check_page_alignment.py build/android-arm64/core
```

La biblioteca compartida exporta solo las funciones de la API en C y se alinea a páginas de
16 KB, como exige Google Play a las apps que se dirigen a Android 15 (API 35) o posterior en
dispositivos de 64 bits (RNF-20).

## API en C

Las apps hablan con el núcleo mediante comandos JSON. Una sesión se usa desde un solo hilo, y
todo búfer devuelto se libera con `mc_buffer_free`:

```c
mc_session* session = NULL;
uint8_t* error = NULL;
size_t error_len = 0;
if (mc_session_create(pack, pack_len, &session, &error, &error_len) != MC_OK) {
    /* error: {"ok":false,"error":"bad_pack","detail":"..."} */
    mc_buffer_free(error);
    return;
}

const char* command = "{\"cmd\":\"validate_deck\",\"deck\":{\"seeker\":\"...\",\"main\":[...]}}";
uint8_t* reply = NULL;
size_t reply_len = 0;
mc_session_dispatch(session, (const uint8_t*)command, strlen(command), &reply, &reply_len);
/* reply: {"ok":true,"valid":false,"issues":[{"rule":"deck_too_small","detail":"..."}]} */
mc_buffer_free(reply);
mc_session_destroy(session);
```

| Comando | Respuesta |
|---|---|
| `{"cmd":"pack_info"}` | `{"ok":true,"pack":{"id","version","date","origin","license","cards"}}` |
| `{"cmd":"validate_deck","deck":{...}}` | `{"ok":true,"valid":bool,"issues":[{"rule","card","detail"}]}` |

El mazo tiene la forma `{"seeker","legacy","terrain","main":[{"card","copies"}],"side":[...]}`.
Cada incidencia lleva un código estable que la app traduce: `seeker_missing`,
`seeker_wrong_type`, `legacy_missing`, `legacy_wrong_type`, `legacy_mismatch`, `terrain_missing`,
`terrain_wrong_type`, `unknown_card`, `invalid_copies`, `card_type_not_allowed`,
`rank_not_allowed`, `sigil_mismatch`, `too_many_copies`, `deck_too_small`, `deck_too_large` y
`side_too_large`.

## Integración continua

Los flujos de [`.woodpecker/`](.woodpecker) llaman a `tools/ci/ci.sh`, que también se puede
ejecutar en local. Para no gastar más recursos de Codeberg de los necesarios, lo pesado va aparte:

| Flujo | Cuándo | Qué |
|---|---|---|
| `core.yaml` | En cada push y pull request, salvo si solo cambia documentación | clang-format y clang-tidy, ASan y UBSan, cobertura mínima del 90 % (RNF-17) y licencias de las dependencias |
| `extended.yaml` | Una vez por semana o a mano | ThreadSanitizer, Release, fuzzing y biblioteca para Android con comprobación de 16 KB (RNF-19 y RNF-20) |
| `licenses.yaml` | Cabeceras SPDX en cada push; ScanCode, una vez por semana o a mano | Licencias del código propio (RNF-15) |

Para la ejecución semanal, crea en Woodpecker una tarea programada (Cron) llamada `semanal`.

## Licencia

El código es software libre bajo la [Licencia Apache 2.0](LICENSE); cada fichero lleva su
cabecera SPDX. Las dependencias tienen licencias compatibles (ADR-08); ver [`NOTICE`](NOTICE).
Malediction y todo su contenido pertenecen a Loot Studios.
