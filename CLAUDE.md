# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

`AGENTS.md` holds the contributor-facing conventions (coding style, commit/PR guidelines). This file
adds the architecture and workflow detail that only emerges from reading across several files.

## Build & Test

Windows-only, **64-bit (x64)** build with Qt 6.8.3 and Visual Studio 2022. For prerequisites, configuration, build commands, and test
commands, read `Docs/Build.md` before setting up or rebuilding the project.

```powershell
# Run a single Qt Test slot directly after building its executable.
./Build/Binary/AirPodsDomainTests.exe MergesAdvertisementsFromBothSides

clang-format -i Source\Path\File.cpp
```

Binaries and deployed Qt files land in `Build/Binary/`.

With `APD_BUILD_TESTS=ON`, CTest registers `AirPodsDomainTests`, `QuickConnectTests`,
`AnimationViewTests`, and `UiRenderingTests`; `Tests/CMakeLists.txt` is the source of truth.
The domain and quick-connect suites use `QCoreApplication`. Animation and UI rendering tests
create widgets and need an interactive Windows desktop; animation tests also exercise the
Windows multimedia backend. Qt runtime DLLs must be deployed beside the executables or on `PATH`.

| CMake option | Default | Effect |
|---|---|---|
| `APD_BUILD_TESTS` | OFF | Builds `Tests/` and enables CTest |
| `APD_ENABLE_CONSOLE` | OFF | Console subsystem + `APD_ENABLE_CONSOLE` define, for diagnostics |
| `APD_GENERATE_INSTALLER` | OFF | Runs CPack/NSIS post-build (needs NSIS installed) |
| `APD_QT_DEPLOY` | ON | Runs `windeployqt` post-build |
| `APD_GITHUB_OWNER` / `APD_GITHUB_REPOSITORY` | `SpriteOvO` / `AirPodsDesktop` | Repository the updater checks; set these for fork builds |
| `APD_BUILD_GIT_HASH` | — | Stamps the build hash into the log banner |

Compiler quirks baked into `CMakeLists.txt`: `CMAKE_CXX_STANDARD 20` is remapped to `/std:c++latest`
because MSVC's `<format>` is not available under `/std:c++20`, and `/await` is forced globally as a
workaround for cppwinrt under C++20. Both are load-bearing — the code uses `std::format` and WinRT.

`Source/Config.h` is generated from `Config.h.in` into `${PROJECT_BINARY_DIR}/Source` and is included
as `<Config.h>` (angle brackets), not a relative path.

## Target Layering

Six targets with a strict, enforced dependency direction. Keep platform calls and Qt Widgets out of
the lower layers.

```
apd_support ──▶ apd_domain ──┬──▶ apd_core ─────────┐
                             │                      ├──▶ apd_gui ──▶ AirPodsDesktop (exe)
                             └──▶ apd_presentation ─┘
```

| Target | Contents | May depend on |
|---|---|---|
| `apd_support` | `Opts`, `Logger`, `Assert`, `Error` | Qt Core/Gui, spdlog, cxxopts, magic_enum, boost::stacktrace |
| `apd_domain` | `AirPodsAdvertisement`, `AirPodsStateManager`, `AppleCP` — pure protocol/state logic | `apd_support` only |
| `apd_core` | `AirPods::Manager`, `Settings`, `SettingsRepository`, `Update`, `LowAudioLatency`, `Debug`, and the `_win.cpp` platform impls | `apd_domain`, Qt Multimedia, cpr, nlohmann_json, boost::pfr |
| `apd_presentation` | `MainWindowPresentation` view models — plain state→struct mapping, no widgets | `apd_domain` only |
| `apd_gui` | `MainWindow`, `TrayIcon`, `TaskbarStatus`, `SelectWindow`, `UpdateWindow`, `SettingsWindow`, `Theme`, `AnimationPlayback`, `Widget::Battery`, `Widget::AnimationView` | `apd_core` + `apd_presentation`, Qt Widgets/Svg/Multimedia |
| `AirPodsDesktop` | `Main.cpp`, `Application.cpp`, resources | `apd_gui` + SingleApplication |

Domain and view-model tests link `apd_core` + `apd_presentation` + Qt Test and run without a
Bluetooth radio or a window. Animation and UI rendering tests additionally link `apd_gui`.

## Runtime Data Flow

1. `Core::Bluetooth::AdvertisementWatcher` (WinRT `BluetoothLEAdvertisementWatcher`) receives BLE
   advertisements on a WinRT thread and self-restarts on failure every 3s.
2. `AirPods::Manager::OnAdvertisementReceived` filters via `Details::Advertisement::IsDesiredAdv` —
   manufacturer data must carry Apple's vendor id `76` and pass `AppleCP::AirPods::IsValid`.
3. `Details::Advertisement` parses the fixed 27-byte `ProximityPairing` packet into an `AdvState`.
4. `Details::StateManager::OnAdvReceived` merges the two sides, applies the `rssi_min` floor, and
   arms 10s "lost" / per-side reset timers. It emits an `UpdateEvent { oldState, newState }`.
5. `Manager` raises Qt signals — `StateUpdated`, `Disconnected`, `LidToggled`,
   `ScannerAvailabilityChanged`, `BoundDeviceUnavailable`.
6. `ApdApplication::ConnectAirPodsManager()` is the single fan-out point: it wires those signals to
   `MainWindow`, `TrayIcon`, and `TaskbarStatus`. Look there first to trace any UI update.
7. Ear detection: `Manager::OnBothInEar` calls `Core::GlobalMedia::Play()` / `Pause()` (Windows
   `GlobalSystemMediaTransportControls`) when the setting is on.

### Apple Continuity Protocol quirks

`Source/Core/AppleCP.h` documents these at length; they explain most of the domain's apparent
weirdness:

- The packet has no left/right fields — only `curr`/`anot` (broadcasting earbud vs. the other one).
  A `broadcastFrom` bit says which is which, so accessors are "flipped" depending on the side.
- One pair of AirPods may appear as **one or two** discoverable BLE devices depending on lid and
  in-ear state; the case has no radio of its own. `StateManager` merges both sides into one `State`.
- AirPods use random non-resolvable BLE addresses, so a device cannot be identified by address.
  That is why binding goes through the classic-Bluetooth `device_address` setting plus a device
  lookup thread, and why `StateManager` falls back to heuristics.
- Battery values arrive as 0–10 and are multiplied by 10 into percentages in `Advertisement`; an
  out-of-range value means "unavailable" (`Core::AirPods::Battery` wraps an `std::optional`).

## Settings System

`SETTINGS_FIELDS(callback)` in `Source/Core/Settings.h` is an X-macro and the single source of truth.
One line there generates both the `Fields` struct member and the `MetaFields` metadata entry
(name, `Impl::OnApply` callback, `Impl::Desc`, `Impl::Sensitive`, `Impl::Deprecated`). `boost::pfr`
iterates `MetaFields` for load, save, and apply — no field lists to keep in sync.

Adding a setting:

1. Add one `callback(...)` line to `SETTINGS_FIELDS`.
2. If it has a side effect, declare and define `OnApply_<name>` in `Settings.h`/`.cpp`, add a method
   to `Core::Settings::ApplyObserver`, and implement it in `ApdApplication`.

`ApplyObserver` exists so `apd_core` never includes GUI headers — Core raises the intent, the
application layer performs it. Persistence goes through `Core::Settings::Repository`
(`QSettings`-backed in production, `MemoryRepository` injected via `SetRepository()` in tests).

Two rules with teeth:

- Never delete an obsolete field — mark it `Impl::Deprecated()` and leave it in place.
- Bump `kFieldsAbiVersion` when a key's name or type changes incompatibly. A mismatch makes
  `Settings::Load()` return `AbiIncompatible`, which drops the user into the first-time-use wizard.

`ModifiableAccess()` returns an RAII accessor holding the settings mutex; it saves and applies the
diff in its destructor, so mutate through it rather than calling `Save()` by hand.

## Presentation Layer

`Gui::MainWindowViewModel` (in `apd_presentation`) consumes `Core::AirPods::State` plus lifecycle
calls (`Available`/`Unavailable`/`Disconnect`/`Bind`/`Unbind`) and produces a plain comparable
`MainWindowPresentation` struct. `MainWindow` only renders that struct. New main-window display logic
belongs in the view model, where `Tests/AirPodsDomainTests.cpp` can cover it without a widget.

`GetAnimationPresentation(Model)` maps a model to its `qrc:/Resource/Video/*.avi` path and source size.

## Threading & Qt Conventions

BLE callbacks, the device-lookup `std::jthread`, and the hourly `Update::AsyncChecker` all run off the
GUI thread. GUI classes therefore expose `…Safely()` signals — `UpdateStateSafely`, `ShowSafely`,
`HideSafely`, `BindSafely`, `SetTranslatorSafely`, `ControlSafely` — that worker threads emit so Qt
queues the work onto the GUI thread. **Never touch a widget directly from a worker thread; emit the
`*Safely` signal instead.** Custom types crossing a queued connection need `qRegisterMetaType` (see
`Core::AirPods::State` in `ApdApplication::Prepare`).

Shared primitives live in `Source/Helper.h`: `Helper::Timer`, `Helper::ConWorker`, `Helper::Callback`,
`Helper::Sides<T>` (a left/right pair), `Helper::Singleton`, `Helper::ToString<T>`.

Logging is `LOG(Info, "fmt {}", args)` (spdlog, compiled in at trace level; `--trace` enables it at
runtime). Add a `Helper::ToString<T>` specialization to make a domain type loggable. Failures use
`APD_ASSERT(cond)` and `FatalError(msg, report)`.

## Platform Abstraction

Windows code is isolated by a consistent file convention:

- `Foo.h` — public header; includes `Foo_win.h` under `#if defined APD_OS_WIN`.
- `Foo_abstract.h` — pure-virtual or CRTP interface shared by all platforms.
- `Foo_win.h` / `Foo_win.cpp` — WinRT/Win32 implementation, guarded by `#error` if compiled elsewhere.

Used by `Bluetooth` and `GlobalMedia`. `AutoStart` instead uses a factory
(`Core::AutoStart::CreateAutoStartService()`) returning an interface. `_win.cpp` files are only added
to the source lists inside `if (WIN32)` in `CMakeLists.txt`. WinRT helpers live in
`Source/Core/OS/Windows.h`.

## Translations

Locales are listed in `APD_TRANSLATION_LOCALES` in `CMakeLists.txt`; `.ts` files live in
`Source/Resource/Translation/`, and `.qm` files are generated into `Build/Binary/translations/`.

A custom `APD_CREATE_UPDATE_TS` target runs `lupdate -recursive Source/ -no-obsolete -locations none`
only when requested. Normal builds compile the existing `.ts` files to `.qm` without rewriting
tracked translations. Widgets call `UTILS_QT_REGISTER_LANGUAGECHANGE` to retranslate live.

Adding a locale is a CMake edit plus a rebuild; see `CONTRIBUTING.md` for the translator workflow and
the `--print-all-locales` launch option.

## Updater & Release

`Core::Update` queries the GitHub API for `APD_GITHUB_OWNER/APD_GITHUB_REPOSITORY`, rejects metadata
from any other repository, selects the `.exe` release asset matching the compiled CPack architecture,
and verifies its SHA-256 against the release digest before executing it. `AsyncChecker` polls hourly.
Qt 6 builds select `win64`; legacy clients select the signed `win32-bridge` alias.

The version must be identical in `project(... VERSION ...)` in `CMakeLists.txt` and `version-string`
in `vcpkg.json`; a `v*.*.*` tag must match both. CI fails the build otherwise. Full process is in
`Docs/Release.md`.

## Adding an AirPods Model

1. Add the enum value to `Core::AirPods::Model` and a case to `Helper::ToString<Model>` — both in
   `Source/Core/Base.h`.
2. Map the BLE model id in `AppleCP::AirPods::GetModel(uint16_t)` in `Source/Core/AppleCP.cpp`.
3. Add the animation case to `GetAnimationPresentation` in `Source/Gui/MainWindowPresentation.cpp`.
   A model with no artwork of its own can borrow the nearest existing one instead.
4. Add the `.avi` under `Source/Resource/Video/` **and** register it in `Source/Resource/Resource.qrc`.
   `MapsMainWindowAnimationResources` walks every enumerator and fails if a mapping names a resource
   the qrc does not list or that is missing from disk, so an unregistered file is caught.
5. Extend `MapsMainWindowAnimationResources` in `Tests/AirPodsDomainTests.cpp`.

Keep animation source material and conversion tooling outside the repository. Commit only the
generated `.avi` runtime asset needed by the application, then register and test it as described
above. The ten current animations are 360 frames at 60 fps.

Note that `AirPods_4.avi` and `AirPods_4_ANC.avi` are currently byte-identical, so one conversion
ran against the wrong source. Regenerating the ANC one needs the original `AirPods1,5-v2` asset.
