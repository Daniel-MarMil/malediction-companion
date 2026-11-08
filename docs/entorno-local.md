<!--
SPDX-FileCopyrightText: 2026 Daniel Martín Millet
SPDX-License-Identifier: Apache-2.0
-->

# Entorno de desarrollo local

Con este entorno se puede probar en el propio equipo todo lo que hace la integración continua
y empezar a desarrollar la app Android. Está pensado para Windows con WSL 2.

## Cómo se reparte

| Dónde | Qué |
|---|---|
| Windows | El repositorio, Git, Android Studio con el emulador y, si se quiere, Visual Studio para compilar el núcleo con MSVC. |
| WSL 2 con Ubuntu 26.04 | El núcleo: compilación, pruebas, sanitizers, cobertura, fuzzing, análisis y biblioteca para Android. Es la misma versión de Ubuntu que usa la integración continua. |

El código está en una sola copia, la de Windows. Ubuntu trabaja sobre ella a través de `/mnt/c`,
pero compila en `~/.cache/seekers-companion/build`: es más rápido y evita la ruta con espacios
(`My projects`), que da problemas a algunas herramientas de compilación.

## 1. Instalar WSL 2 y Ubuntu 26.04

En PowerShell como administrador:

```powershell
wsl --install -d Ubuntu-26.04
```

Reinicia si lo pide. Al abrir Ubuntu por primera vez, crea tu usuario y tu contraseña. Para
comprobarlo, `wsl -l -v` debe mostrar `Ubuntu-26.04` con la versión 2. Si la instalación falla,
actualiza WSL con `wsl --update` y vuelve a intentarlo.

Ubuntu 24.04 también sirve: el núcleo compila y pasa las pruebas con GCC 13 y Clang 18. La
integración continua usa las versiones de 26.04 (GCC 15 y Clang 21), así que allí clang-tidy
puede encontrar cosas que en 24.04 no ve.

## 2. Preparar Ubuntu

En la terminal de Ubuntu:

```sh
cd "/mnt/c/Users/Danie/Documents/My projects/App Malediction"
tools/dev/setup-wsl.sh
source ~/.bashrc
```

El script se ejecuta una vez y se puede repetir para actualizar. Instala:

- los paquetes de compilación y análisis (GCC, Clang, CMake, Ninja, clang-tidy, gdb, Valgrind, cppcheck);
- gcovr, para la cobertura;
- vcpkg, en la versión fijada en `vcpkg.json`;
- el NDK r30 de Android;
- Woodpecker CLI, para validar los flujos de `.woodpecker/`.

Además, deja las variables necesarias en `~/.bashrc`.

## 3. Probar el núcleo

Desde la carpeta del proyecto, en Ubuntu:

```sh
tools/ci/ci.sh asan            # pruebas con AddressSanitizer y UBSan
tools/ci/ci.sh coverage        # cobertura; falla por debajo del 90 %
tools/ci/ci.sh lint            # clang-format y clang-tidy
tools/ci/ci.sh android         # biblioteca para Android y comprobación de 16 KB
tools/ci/ci.sh all             # todo lo que hace la integración continua
```

La primera vez, vcpkg compila las dependencias y tarda unos minutos; después quedan en caché.
`tools/ci/ci.sh` sin argumentos muestra todas las tareas.

Para abrir el informe de cobertura en el navegador de Windows:

```sh
explorer.exe "$(wslpath -w "$MC_BUILD_ROOT/coverage/html/index.html")"
```

Para validar los flujos de integración continua antes de subirlos:

```sh
woodpecker-cli lint --strict .woodpecker/core.yaml
```

### Si vcpkg no puede descargar las dependencias

Un error como `Could not resolve hostname` al descargar es un problema de DNS de WSL, no del
proyecto; los errores de CMake y Ninja que salen después son consecuencia de ese fallo. Se
confirma si `getent hosts github.com` no devuelve nada.

1. En Windows, crea o edita `%UserProfile%\.wslconfig` con este contenido:
   ```ini
   [wsl2]
   dnsTunneling=true
   networkingMode=mirrored
   ```
   Las dos opciones necesitan Windows 11 22H2 o posterior; `dnsTunneling` ya viene activada por
   defecto en esas versiones. Después, en PowerShell, `wsl --shutdown`, y vuelve a abrir Ubuntu.
2. Si sigue fallando (por ejemplo, con una VPN), fija el DNS dentro de Ubuntu. Añade a
   `/etc/wsl.conf` (se edita con `sudo`, por ejemplo `sudo nano /etc/wsl.conf`):
   ```ini
   [network]
   generateResolvConf = false
   ```
   Ejecuta `wsl --shutdown` en PowerShell, abre Ubuntu y crea el archivo con un servidor DNS,
   por ejemplo el de Quad9:
   ```sh
   sudo rm -f /etc/resolv.conf
   echo "nameserver 9.9.9.9" | sudo tee /etc/resolv.conf
   ```

Cuando `getent hosts github.com` responda, repite la tarea que falló.

## 4. Android Studio en Windows

1. Instala la versión estable de [Android Studio](https://developer.android.com/studio). A 7-11-2026
   es Rabbit 2 (2026.2.2).
2. En el SDK Manager, instala Android SDK Platform 36 o posterior (36 es el nivel que exige Google
   Play desde el 31-08-2026), Android SDK Platform-Tools, Android Emulator y una imagen del sistema
   x86_64 del mismo nivel.
3. Activa la aceleración del emulador. Con WSL 2 instalado, la opción recomendada es la Plataforma
   del hipervisor de Windows (WHPX): en «Activar o desactivar las características de Windows»,
   marca «Plataforma del hipervisor de Windows» y reinicia. Para comprobarla:
   `%LOCALAPPDATA%\Android\Sdk\emulator\emulator -accel-check` debe mencionar WHPX.
4. Crea un dispositivo virtual con esa imagen y comprueba que arranca.

Para las pruebas con TalkBack de la fase 1 conviene también un móvil real con la depuración USB
activada.

## 5. Editor para el núcleo (opcional)

VS Code con la extensión WSL trabaja directamente dentro de Ubuntu: desde la carpeta del proyecto,
`code .` lo abre conectado a WSL. Para el C++, la extensión clangd o la C/C++ de Microsoft, junto con
CMake Tools. clangd necesita la base de datos de compilación: añade a sus argumentos
`--compile-commands-dir=/home/TU_USUARIO/.cache/seekers-companion/build/asan`, con tu usuario
de Ubuntu.

## 6. Compilar con MSVC en Windows (opcional)

Sirve para comprobar que el núcleo también compila con el compilador de Microsoft.

1. Instala Visual Studio 2026 (Community o Build Tools) con la carga de trabajo «Desarrollo para el
   escritorio con C++».
2. Instala vcpkg en una ruta sin espacios y define `VCPKG_ROOT`:
   ```powershell
   git clone https://github.com/microsoft/vcpkg C:\dev\vcpkg
   C:\dev\vcpkg\bootstrap-vcpkg.bat -disableMetrics
   setx VCPKG_ROOT C:\dev\vcpkg
   ```
3. Abre la carpeta del proyecto en Visual Studio con la integración de `CMakePresets.json` activada
   (Herramientas > Opciones > CMake > General), o usa la «Developer PowerShell»:
   ```powershell
   cmake --preset dev
   cmake --build --preset dev
   ctest --preset dev
   ```

## 7. Diseño de la interfaz

- **Bocetos:** [Penpot](https://penpot.app) es software libre (MPL 2.0) y funciona en el navegador o
  en un servidor propio.
- **Pantallas reales:** las vistas previas de Jetpack Compose en Android Studio.
- **Tema:** los colores, tipografías e iconos oficiales llegan con el paquete privado de
  `content/official/` (RNF-22); mientras tanto, la app usa un tema neutro.

## Lista de comprobación

- [ ] `wsl -l -v` muestra `Ubuntu-26.04` con la versión 2.
- [ ] `tools/ci/ci.sh asan` pasa todas las pruebas.
- [ ] `tools/ci/ci.sh android` genera `libmc_core.so` y supera la comprobación de 16 KB.
- [ ] El emulador arranca y `emulator -accel-check` muestra WHPX.
- [ ] (Opcional) `ctest --preset dev` pasa en Windows con MSVC.
