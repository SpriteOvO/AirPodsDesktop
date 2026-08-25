# Repository Guidelines

## Project Structure & Module Organization

AirPodsDesktop is a Windows-focused C++20 and Qt 5 application built with CMake. Application startup and shared utilities live directly under `Source/`. Platform-independent behavior is grouped in `Source/Core/`, while Windows implementations use the `_win.cpp` suffix. Qt windows, widgets, and `.ui` forms belong in `Source/Gui/`. Images, audio, videos, translations, and Qt/Windows resource manifests are under `Source/Resource/`. Keep CMake helpers in `CMake/`, build documentation in `Docs/`, and screenshots or promotional assets in `Assets/`. Generated output belongs in `Build/` and must not be committed.

Respect the target dependency direction: `apd_support` → `apd_domain` → `apd_core`, with `apd_presentation` feeding `apd_gui`. Keep platform calls and widgets out of domain code.

## Build, Test, and Development Commands

Use PowerShell with CMake 3.20+, Visual Studio 2019, Qt 5.15.2 (`msvc2019`), and a bootstrapped vcpkg checkout.

```powershell
cmake -S . -B Build -G "Visual Studio 16 2019" -A Win32 `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -DCMAKE_PREFIX_PATH=C:\Qt\5.15.2\msvc2019
cmake --build Build --config RelWithDebInfo
```

Executables and deployed Qt files appear in `Build/Binary/`. Add `-DAPD_ENABLE_CONSOLE=ON` for console diagnostics or `-DAPD_GENERATE_INSTALLER=ON` when NSIS is installed. Run `clang-format -i Source\path\File.cpp` on touched C++ files.

## Coding Style & Naming Conventions

Follow `.clang-format`: four-space indentation, no tabs, 100-column limit, and custom brace wrapping. Use `PascalCase` for classes and methods, `camelCase` for local variables, and a leading underscore for private data members (for example, `_stateMgr`). Match headers and implementations (`AirPods.h` / `AirPods.cpp`), and preserve the repository's include grouping and namespace style.

## Testing Guidelines

First-party Qt Test coverage lives in `Tests/` and is enabled with `-DAPD_BUILD_TESTS=ON`. Build the selected configuration, then run `ctest --test-dir Build -C RelWithDebInfo --output-on-failure`. Every change must at least compile in Win32 `RelWithDebInfo`; manually exercise affected Bluetooth, tray, media, settings, or translation flows. Name new test files after the unit or feature under test, such as `AirPodsStateManagerTest.cpp`.

## Commit & Pull Request Guidelines

Recent commits favor Conventional Commit prefixes such as `feat:`, `fix:`, and `refactor:`; use an imperative, scoped summary and keep unrelated changes separate. Pull requests should explain the motivation and behavior change, link relevant issues, list build/manual-test results, and include screenshots or recordings for UI changes. Call out new dependencies, configuration changes, and affected Windows or AirPods models.
