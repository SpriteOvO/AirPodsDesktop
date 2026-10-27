# AGENTS.md

AirPodsDesktop: a Windows x64 tray app (C++20, Qt 6.8.3, CMake, vcpkg) that reads AirPods battery
and lid/ear state from Apple BLE advertisements. Windows-only; there is no Linux or macOS build.

## Critical Rules

- **Never touch a widget from a worker thread** (BLE callbacks, the device-lookup `std::jthread`,
  `Update::AsyncChecker`). **Do** emit the class's `…Safely()` signal so Qt queues it to the GUI
  thread, and `qRegisterMetaType` any new type that crosses the queued connection.
- **Never put Qt Widgets, Win32, or WinRT into `apd_support`, `apd_domain`, or `apd_presentation`.**
  **Do** put platform code in `apd_core` `*_win.cpp` files and widgets in `apd_gui`; when Core needs
  a GUI side effect, add a method to `Core::Settings::ApplyObserver` and implement it in
  `ApdApplication`.
- **Never delete or rename a settings field.** **Do** mark it `Impl::Deprecated()` in
  `SETTINGS_FIELDS`, and bump `kFieldsAbiVersion` for an incompatible key/type change (a mismatch
  sends every user back to the first-run wizard, so treat a bump as a user-visible decision).
- **Never call `Settings::Save()` by hand.** **Do** mutate through `ModifiableAccess()`; its
  destructor saves and applies the diff.
- **Never relax `Core::Update`'s repository check or SHA-256 verification**, even to make a fork
  or test build update. The updater executes the installer it downloads, so these checks are what
  stop a wrong or tampered installer from running. **Do** stop and ask the user when a task seems
  to need them changed; fork builds use `-DAPD_GITHUB_OWNER` / `-DAPD_GITHUB_REPOSITORY` instead.
- **Never build the `APD_CREATE_UPDATE_TS` target unless asked.** It runs `lupdate` and rewrites all
  13 `.ts` files. **Do** edit the needed `.ts` entry directly; a normal build compiles `.qm` files.
- **Never bump the version in one file.** **Do** change `project(... VERSION ...)` in
  `CMakeLists.txt` and `version-string` in `vcpkg.json` together; CI fails on a mismatch.
- **Never commit `Build/`, animation source material, conversion scripts, or `Docs/ReleaseNotes/`.**
  **Do** commit only the generated `.avi` and register it in `Source/Resource/Resource.qrc`.
- **Never name deployed runtime files with `test`, `mock`, `simulator`, `fixture`, `harness`, or
  `validation`.** CI's packaging step rejects the release if such a path ships.
- **Never remove `/std:c++latest` or `/await` from `CMakeLists.txt`.** MSVC's `<format>` and
  cppwinrt need them.

## Commands

PowerShell from the repository root. `VCPKG_ROOT` must point at a bootstrapped vcpkg checkout and
`QT_ROOT_DIR` at the Qt 6.8.3 `msvc2022_64` folder (CI sets `QT_ROOT_DIR` itself).

```powershell
# Configure once per build directory (use a fresh directory when changing Qt or generator)
cmake -S . -B Build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DCMAKE_PREFIX_PATH="$env:QT_ROOT_DIR" `
  -DAPD_BUILD_TESTS=ON

# Build (outputs and deployed Qt DLLs land in Build/Binary/, no per-config subfolder)
cmake --build Build --config RelWithDebInfo --parallel

# All registered tests, including UI scale variants; needs an interactive Windows desktop
ctest --test-dir Build -C RelWithDebInfo --output-on-failure

# One Qt Test slot, for a fast loop while iterating
./Build/Binary/AirPodsDomainTests.exe MergesAdvertisementsFromBothSides

# Format only the files you touched
clang-format -i Source\Core\AirPods.cpp

# Run with trace logging; reconfigure with -DAPD_ENABLE_CONSOLE=ON to see it in a console
./Build/Binary/AirPodsDesktop.exe --trace
```

`-DAPD_GENERATE_INSTALLER=ON` needs NSIS. For fork builds set `-DAPD_GITHUB_OWNER` and
`-DAPD_GITHUB_REPOSITORY`, or the updater checks upstream.

## Layout

| Path | Contents |
|---|---|
| `Source/` | `Main.cpp`, `Application.cpp` (signal fan-out), `Helper.h` shared primitives |
| `Source/Core/` | Protocol (`AppleCP`, `AirPodsAdvertisement`, `AirPodsStateManager`), settings, updater, `*_win.cpp` |
| `Source/Gui/` | Windows, `.ui` forms, `MainWindowPresentation` view model, `Widget/` |
| `Source/Resource/` | `Resource.qrc`, videos, images, `Translation/*.ts` |
| `Tests/` | Qt Test suites; `Tests/CMakeLists.txt` is the list of registered tests |
| `Docs/` | `Build.md`, `Release.md` |

## Conventions

- Target direction: `apd_support → apd_domain → {apd_core, apd_presentation} → apd_gui → exe`.
- Main-window display logic goes in `MainWindowViewModel` (`apd_presentation`) and is tested in
  `Tests/AirPodsDomainTests.cpp`; `MainWindow` only renders the resulting struct.
- New platform feature: `Foo.h` + `Foo_abstract.h` + `Foo_win.h/.cpp`, with the `_win.cpp` added
  inside `if (WIN32)` in `CMakeLists.txt`.
- Include generated config as `<Config.h>` (it lives in the build tree).
- Log with `LOG(Info, "fmt {}", args)`; add a `Helper::ToString<T>` specialization for new types.
  Fail with `APD_ASSERT(cond)` / `FatalError(msg, report)`.
- Naming: `PascalCase` types and methods, `camelCase` locals, `_member` private fields.
- `Source/Resource/Resource.h` is owned by the Visual Studio resource editor; leave its formatting.
- Commits: Conventional Commits (`feat:`, `fix:`, `docs:`, `chore:`, `test:`, `refactor:`), one
  concern per commit. Branch from `main` with a short topic name and merge through a PR; Codex
  prefixes its branches `codex/<topic>`, other agents use the bare topic.

## Done means

1. x64 `RelWithDebInfo` builds.
2. The full `ctest` run passes: every suite in `Tests/CMakeLists.txt`, animation and UI rendering
   included. Without an interactive desktop the work is not done: report which suites could not
   run and ask the user to run them, rather than calling the change finished.
3. Behavior changes in domain, settings, or view-model code have a new or updated Qt Test slot.
4. A new AirPods model or animation passes `MapsMainWindowAnimationResources`.
5. Touched C++ files are `clang-format`ted.
6. Bluetooth, tray, media, settings, or translation flows you changed are listed in the PR as
   manually tested, or explicitly as not tested.

## Read before working on

- Architecture, data flow, threading, settings X-macro, Apple protocol quirks: `Docs/Architecture.md`
- Adding an AirPods model: `Docs/Architecture.md` → "Adding an AirPods Model"
- Toolchain setup and test environment: `Docs/Build.md`
- Versioning, tagging, signing, draft releases: `Docs/Release.md`
- Translator workflow and `--print-all-locales`: `CONTRIBUTING.md`
- What CI actually runs: `.github/workflows/windows.yml`
