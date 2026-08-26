# Getting started on macOS

## Toolchain baseline

The project uses CMake, Apple Clang, and Qt 6. Build with C++20: it is mature across the intended macOS, Ubuntu 24.04, and current Windows toolchains. Do not use a compiler-specific “latest” language mode in the shared codebase.

Current local baseline verified on 2026-08-26:

| Tool | Version |
| --- | --- |
| Apple Clang | 21.0.0 |
| CMake | 4.4.2 |
| Qt | 6.11.1 |
| Homebrew | 6.0.19 |

Homebrew reported no outdated `cmake`, `qt`, `git`, or `llvm` formulae. Qt is installed under Homebrew, so its CMake configuration is discovered automatically on this machine.

## Build and test

From this repository:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run either application from `build/apps/server/` or `build/apps/client/`. The server chooses a free localhost port and prints it in its visible console.

## Local macOS app bundles

The normal build remains the cross-platform development build. On macOS, create local `.app` bundles only after it succeeds:

```sh
source scripts/commands
build
build-mac-bundles
mac-server
mac-client
```

This creates `BluffSkill Server.app` and `BluffSkill.app` beside the normal executables. The bundles have stable application identifiers for direct AppleScript control and retain separate card-face icons: 7♣ for the server and 2♦ for the client. The local bundles rely on the developer machine's Qt installation; a later distribution step will use Qt's deployment tooling to embed runtime dependencies.

## Cursor

Open `repos/bluffskill` as the workspace folder. Install the CMake Tools and C/C++ extensions, select the Apple Clang kit, and configure the `build` directory. Qt Creator remains useful for examining Qt forms and signal/slot connections, but is not required.
