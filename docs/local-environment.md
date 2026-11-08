<!--
SPDX-FileCopyrightText: 2026 Daniel Martín Millet
SPDX-License-Identifier: Apache-2.0
-->

# Local development environment

With this environment you can run on your own computer everything that continuous integration
does, and start developing the Android app. It is meant for Windows with WSL 2.

## How the work is split

| Where | What |
|---|---|
| Windows | The repository, Git, Android Studio with the emulator and, optionally, Visual Studio to build the core with MSVC. |
| WSL 2 with Ubuntu 26.04 | The core: builds, tests, sanitizers, coverage, fuzzing, analysis and the Android library. It is the same Ubuntu release that continuous integration uses. |

There is a single copy of the code, the one on Windows. Ubuntu works on it through `/mnt/c`, but
builds in `~/.cache/seekers-companion/build`: it is faster and avoids the path with a space
(`My projects`), which some build tools handle badly.

## 1. Install WSL 2 and Ubuntu 26.04

In PowerShell as an administrator:

```powershell
wsl --install -d Ubuntu-26.04
```

Restart if asked. When you open Ubuntu for the first time, create your user and password. To
check it, `wsl -l -v` must show `Ubuntu-26.04` with version 2. If the installation fails, update
WSL with `wsl --update` and try again.

Ubuntu 24.04 also works: the core builds and passes the tests with GCC 13 and Clang 18.
Continuous integration uses the 26.04 versions (GCC 15 and Clang 21), so clang-tidy may find
things there that it does not see on 24.04.

## 2. Set up Ubuntu

In the Ubuntu terminal:

```sh
cd "/mnt/c/Users/Danie/Documents/My projects/App Malediction"
tools/dev/setup-wsl.sh
source ~/.bashrc
```

The script runs once and can be run again to update. It installs:

- the build and analysis packages (GCC, Clang, CMake, Ninja, clang-tidy, clangd, gdb, Valgrind, cppcheck);
- gcovr, for coverage;
- vcpkg, at the version pinned in `vcpkg.json`;
- Android NDK r30;
- Woodpecker CLI, to validate the workflows in `.woodpecker/`.

It also leaves the variables it needs in `~/.bashrc`.

## 3. Test the core

From the project folder, in Ubuntu:

```sh
tools/ci/ci.sh asan            # tests with AddressSanitizer and UBSan
tools/ci/ci.sh coverage        # coverage; fails below 90 %
tools/ci/ci.sh lint            # clang-format and clang-tidy
tools/ci/ci.sh android         # Android library and the 16 KB check
tools/ci/ci.sh all             # everything continuous integration does
```

The first time, vcpkg builds the dependencies and takes a few minutes; after that they are
cached. `tools/ci/ci.sh` without arguments lists every task. [`code-tour.md`](code-tour.md)
explains what the tests cover and how to try the core by hand.

To open the coverage report in the Windows browser:

```sh
explorer.exe "$(wslpath -w "$MC_BUILD_ROOT/coverage/html/index.html")"
```

To validate the continuous integration workflows before pushing them:

```sh
woodpecker-cli lint --strict .woodpecker/core.yaml
```

### If vcpkg cannot download the dependencies

An error such as `Could not resolve hostname` while downloading is a WSL DNS problem, not a
problem with the project; the CMake and Ninja errors that follow are a consequence of it. It is
confirmed if `getent hosts github.com` prints nothing.

1. In Windows, create or edit `%UserProfile%\.wslconfig` with this content:
   ```ini
   [wsl2]
   dnsTunneling=true
   networkingMode=mirrored
   ```
   Both options need Windows 11 22H2 or later; `dnsTunneling` is already on by default in those
   versions. Then run `wsl --shutdown` in PowerShell and open Ubuntu again.
2. If it still fails (for example, with a VPN), set the DNS server inside Ubuntu. Add this to
   `/etc/wsl.conf` (edit it with `sudo`, for example `sudo nano /etc/wsl.conf`):
   ```ini
   [network]
   generateResolvConf = false
   ```
   Run `wsl --shutdown` in PowerShell, open Ubuntu and create the file with a DNS server, for
   example Quad9's:
   ```sh
   sudo rm -f /etc/resolv.conf
   echo "nameserver 9.9.9.9" | sudo tee /etc/resolv.conf
   ```

When `getent hosts github.com` answers, run the task that failed again.

## 4. Android Studio on Windows

1. Install the stable release of [Android Studio](https://developer.android.com/studio). On
   2026-11-07 it is Rabbit 2 (2026.2.2).
2. In the SDK Manager, install Android SDK Platform 36 or later (36 is the level Google Play has
   required since 2026-08-31), Android SDK Platform-Tools, Android Emulator and an x86_64 system
   image of the same level.
3. Turn on emulator acceleration. With WSL 2 installed, the recommended option is the Windows
   Hypervisor Platform (WHPX): in "Turn Windows features on or off", check "Windows Hypervisor
   Platform" and restart. To check it,
   `%LOCALAPPDATA%\Android\Sdk\emulator\emulator -accel-check` must mention WHPX.
4. Create a virtual device with that image and check that it boots.

For the TalkBack tests in phase 1, a real phone with USB debugging turned on is also useful.

## 5. VS Code from WSL

VS Code runs on Windows and connects to Ubuntu: the files, the terminal, Git and the language
extensions all work inside WSL, with the same tools that `tools/ci/ci.sh` uses.

1. Install [VS Code](https://code.visualstudio.com) on **Windows**, not in Ubuntu. In the
   installer, keep **Add to PATH** checked so that the `code` command also works from Ubuntu.
2. In VS Code, install the **WSL** extension (`ms-vscode-remote.remote-wsl`).
3. Open the project from the Ubuntu terminal:
   ```sh
   cd "/mnt/c/Users/Danie/Documents/My projects/App Malediction"
   code .
   ```
   The first time, VS Code installs its server in Ubuntu. When the window is connected, the
   bottom-left corner shows `WSL: Ubuntu-26.04`. If Ubuntu does not find `code`, close the
   Ubuntu terminal and open a new one.
4. If VS Code asks whether you trust the authors of the folder, trust it: in restricted mode the
   clangd extension ignores its arguments.
5. In that window, install the **clangd** extension (`llvm-vs-code-extensions.vscode-clangd`).
   It installs on the Ubuntu side and gives completion, navigation, errors as you type and the
   clang-tidy checks from `.clang-tidy`. Do not use it together with Microsoft's C/C++ extension:
   both try to provide the same features. It uses the `clangd` that `setup-wsl.sh` installs
   (or `sudo apt install clangd`).
6. clangd needs the compilation database that CMake writes when it configures a build. Run
   `tools/ci/ci.sh asan` once, open the Command Palette (F1), run **Preferences: Open Remote
   Settings (JSON)** and add the following, with your Ubuntu user name (`echo $HOME` shows the
   path):
   ```json
   {
     "clangd.arguments": [
       "--compile-commands-dir=/home/YOUR_USER/.cache/seekers-companion/build/asan",
       "--clang-tidy"
     ]
   }
   ```
   These settings only apply to windows connected to WSL. Then run **clangd: Restart language
   server** from the Command Palette.

Use the terminal inside VS Code (**Terminal > New Terminal**) to build and test: it is a Bash
terminal in Ubuntu that reads `~/.bashrc`, so `VCPKG_ROOT`, `MC_BUILD_ROOT` and
`ANDROID_NDK_HOME` are set. Other VS Code processes do not read `~/.bashrc`, which is why this
guide does not use VS Code tasks or CMake Tools to build. `.vscode/` is in `.gitignore`, so
personal settings never reach the repository.

## 6. Build with MSVC on Windows (optional)

This checks that the core also builds with Microsoft's compiler.

1. Install Visual Studio 2026 (Community or Build Tools) with the "Desktop development with C++"
   workload.
2. Install vcpkg in a path without spaces and set `VCPKG_ROOT`:
   ```powershell
   git clone https://github.com/microsoft/vcpkg C:\dev\vcpkg
   C:\dev\vcpkg\bootstrap-vcpkg.bat -disableMetrics
   setx VCPKG_ROOT C:\dev\vcpkg
   ```
3. Open the project folder in Visual Studio with the `CMakePresets.json` integration enabled
   (Tools > Options > CMake > General), or use the "Developer PowerShell":
   ```powershell
   cmake --preset dev
   cmake --build --preset dev
   ctest --preset dev
   ```

## 7. User interface design

- **Sketches:** [Penpot](https://penpot.app) is free software (MPL 2.0) and runs in the browser or
  on your own server.
- **Real screens:** the Jetpack Compose previews in Android Studio.
- **Theme:** the official colors, fonts and icons come with the private pack in
  `content/official/` (RNF-22); until then, the app uses a neutral theme.

## Checklist

- [ ] `wsl -l -v` shows `Ubuntu-26.04` with version 2.
- [ ] `tools/ci/ci.sh asan` passes every test.
- [ ] `tools/ci/ci.sh android` produces `libmc_core.so` and passes the 16 KB check.
- [ ] VS Code opens the project with `code .` and clangd finds the headers.
- [ ] The emulator boots and `emulator -accel-check` shows WHPX.
- [ ] (Optional) `ctest --preset dev` passes on Windows with MSVC.
