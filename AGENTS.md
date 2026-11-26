# Repository Guidelines

## Project Structure & Module Organization

AirPodsDesktop is a C++20 and Qt 6 application built with CMake for **Windows x64** and **Linux x64**. Application startup and shared utilities live directly under `Source/`. Platform-independent behavior is grouped in `Source/Core/`, while platform implementations use the `_win.cpp` and `_linux.cpp` suffixes. Qt windows, widgets, and `.ui` forms belong in `Source/Gui/`. Images, audio, videos, translations, and Qt/Windows resource manifests are under `Source/Resource/`. Keep CMake helpers in `CMake/`, build documentation in `Docs/`, and screenshots or promotional assets in `Assets/`. Generated output belongs in `Build/` (or `Build-linux/`) and must not be committed.

Respect the target dependency direction: `apd_support` → `apd_domain` → `apd_core`, with `apd_presentation` feeding `apd_gui`. Keep platform calls and widgets out of domain code.

## Build, Test, and Development Commands

Two targets are supported: **Windows x64** (Visual Studio 2022, Qt 6.8.3 `msvc2022_64`, vcpkg) and **Linux x64** (GCC 13+, Qt 6.8.3 `gcc_64`, BlueZ/D-Bus, `libpulse`). See `Docs/Build.md` for full prerequisites.

### Windows (PowerShell)

```powershell
cmake -S . -B Build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 -DAPD_BUILD_TESTS=ON
cmake --build Build --config RelWithDebInfo
ctest --test-dir Build -C RelWithDebInfo --output-on-failure
```

### Linux (Ubuntu 24.04+)

```bash
cmake -S . -B Build-linux -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH=~/.local/Qt/6.8.3/gcc_64 -DAPD_BUILD_TESTS=ON
cmake --build Build-linux -j"$(nproc)"
xvfb-run -a ctest --test-dir Build-linux --output-on-failure
```

Executables and deployed Qt files appear in `Build/Binary/` (or `Build-linux/Binary/`). Add `-DAPD_ENABLE_CONSOLE=ON` for Windows console diagnostics or `-DAPD_GENERATE_INSTALLER=ON` to build installers (NSIS on Windows, DEB/TGZ via `cpack` on Linux). Run `clang-format -i Source/path/File.cpp` on touched C++ files.

## Coding Style & Naming Conventions

Follow `.clang-format`: four-space indentation, no tabs, 100-column limit, and custom brace wrapping. Use `PascalCase` for classes and methods, `camelCase` for local variables, and a leading underscore for private data members (for example, `_stateMgr`). Match headers and implementations (`AirPods.h` / `AirPods.cpp`), and preserve the repository's include grouping and namespace style.

## Testing Guidelines

First-party Qt Test coverage lives in `Tests/`. Configure with `-DAPD_BUILD_TESTS=ON`, build the selected configuration, then run `ctest --test-dir Build -C RelWithDebInfo --output-on-failure`. Every change must at least compile in x64 `RelWithDebInfo`; manually exercise affected Bluetooth, tray, media, settings, or translation flows. Name new test files after the unit or feature under test, such as `AirPodsStateManagerTest.cpp`.

## Commit & Pull Request Guidelines

Recent commits favor Conventional Commit prefixes such as `feat:`, `fix:`, and `refactor:`; use an imperative, scoped summary and keep unrelated changes separate. Pull requests should explain the motivation and behavior change, link relevant issues, list build/manual-test results, and include screenshots or recordings for UI changes. Call out new dependencies, configuration changes, and affected Windows or AirPods models.
