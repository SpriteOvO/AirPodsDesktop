# Architecture

Architecture and workflow detail that only emerges from reading across several files. The rules
themselves (Critical Rules, Conventions, Done means) live in `/AGENTS.md`; this file explains the
mechanisms behind them.

## Build Configuration

| CMake option | Default | Effect |
|---|---|---|
| `APD_BUILD_TESTS` | OFF | Builds `Tests/` and enables CTest |
| `APD_ENABLE_CONSOLE` | OFF | Console subsystem + `APD_ENABLE_CONSOLE` define, for diagnostics |
| `APD_GENERATE_INSTALLER` | OFF | Runs CPack/NSIS post-build (needs NSIS installed) |
| `APD_QT_DEPLOY` | ON | Runs `windeployqt` post-build |
| `APD_GITHUB_OWNER` / `APD_GITHUB_REPOSITORY` | `SpriteOvO` / `AirPodsDesktop` | Repository the updater checks; set these for fork builds |
| `APD_RELEASE_TAG_PREFIX` | `v` | Tag prefix used only for the Settings window's version link |
| `APD_BUILD_GIT_HASH` | — | Stamps the build hash into the log banner |

`CMAKE_CXX_STANDARD 20` is remapped to `/std:c++latest` because MSVC's `<format>` is not available
under `/std:c++20`, and `/await` is forced globally as a workaround for cppwinrt under C++20.

`Source/Config.h` is generated from `Config.h.in` into `${PROJECT_BINARY_DIR}/Source`, which every
internal target has on its include path; that is why it is included as `<Config.h>`.

## Target Layering

Six targets with a strict dependency direction (the rule is in `AGENTS.md`):

```
apd_support ──▶ apd_domain ──┬──▶ apd_core ─────────┐
                             │                      ├──▶ apd_gui ──▶ AirPodsDesktop (exe)
                             └──▶ apd_presentation ─┘
```

| Target | Contents | May depend on |
|---|---|---|
| `apd_support` | `Opts`, `Logger`, `Assert`, `Error` | Qt Core/Gui, spdlog, cxxopts, magic_enum, boost::stacktrace |
| `apd_domain` | `AirPodsAdvertisement`, `AirPodsStateManager`, `AppleCP` — pure protocol/state logic | `apd_support` only |
| `apd_core` | `AirPods::Manager`, `QuickConnect`, `Settings`, `SettingsRepository`, `Update`, `LowAudioLatency`, `Debug`, and the `_win.cpp` platform impls | `apd_domain`, Qt Multimedia, cpr, nlohmann_json, boost::pfr |
| `apd_presentation` | `MainWindowPresentation` view models — plain state→struct mapping, no widgets | `apd_domain` only |
| `apd_gui` | `MainWindow`, `TrayIcon`, `TaskbarStatus`, `SelectWindow`, `UpdateWindow`, `SettingsWindow`, `Theme`, `AnimationPlayback`, `Widget::Battery`, `Widget::AnimationView` | `apd_core` + `apd_presentation`, Qt Widgets/Svg/Multimedia |
| `AirPodsDesktop` | `Main.cpp`, `Application.cpp`, resources | `apd_gui` + SingleApplication |

Test linkage: `AirPodsDomainTests` links `apd_core` + `apd_presentation` and needs no Bluetooth
radio or window. `QuickConnectTests` links `apd_core` + Qt Widgets. `AnimationViewTests` and
`UiRenderingTests` link `apd_gui` and need an interactive desktop.

## Runtime Data Flow

1. `Core::Bluetooth::AdvertisementWatcher` (WinRT `BluetoothLEAdvertisementWatcher`) receives BLE
   advertisements on a WinRT thread and retries a failed start every 3s (`kRetryInterval`).
2. `AirPods::Manager::OnAdvertisementReceived` filters via `Details::Advertisement::IsDesiredAdv` —
   manufacturer data must carry `AppleCP::VendorId` (76) and pass `AppleCP::AirPods::IsValid`.
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

## Quick Connect

A single tray click can reconnect a chosen audio device instead of opening the main window
(double-click still opens it). It is independent of the BLE pipeline above.

- `Core::QuickConnect::Controller` (`apd_core`) owns the state machine and reports each attempt as
  an `Outcome` through `OutcomeChanged`; `TrayIcon` shows it. `TrayActivation.h` decides whether a
  click means quick-connect or open-window.
- The platform work sits behind `Backend` (`ListDevices`, `RequestReconnect`, `StopObserving`).
  `ApdApplication` injects `WindowsBackend` (`QuickConnect_win.h`) on Windows and `NullBackend`
  elsewhere.
- Settings `tray_quick_connect_enabled` / `tray_quick_connect_device_id` reach the controller via
  `ApplyObserver::OnTrayQuickConnect…Changed`.
- After Windows accepts a reconnect, the controller waits `kDefaultResolveTimeout` (10s) for the
  endpoint before reporting `TimedOut`; tests pass a shorter timeout to drive the real timer.

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

`Settings::Load()` compares the stored ABI version with `kFieldsAbiVersion` and returns
`LoadResult::AbiIncompatible` on a mismatch, which starts the first-time-use wizard.
`ModifiableAccess()` returns an RAII accessor that holds the settings mutex and saves and applies
the diff in its destructor. Deprecation, ABI-bump, and mutation rules: `AGENTS.md` Critical Rules.

## Presentation Layer

`Gui::MainWindowViewModel` (in `apd_presentation`) consumes `Core::AirPods::State` plus lifecycle
calls (`Available`/`Unavailable`/`Disconnect`/`Bind`/`Unbind`) and produces a plain comparable
`MainWindowPresentation` struct that `MainWindow` renders. Because it has no widgets,
`Tests/AirPodsDomainTests.cpp` covers it directly.

`GetAnimationPresentation(Model)` maps a model to its `qrc:/Resource/Video/*.avi` path and source size.

## Threading & Qt Conventions

Work that runs off the GUI thread:

- BLE advertisement callbacks (WinRT thread).
- `AirPods::Manager`'s device-lookup `std::jthread`.
- `MainWindow`'s `_deviceQueryThread`.
- `QuickConnect::Controller`'s device-refresh `std::jthread` and its detached reconnect thread.
- `Update::AsyncChecker`, which polls hourly (`kInterval`).

GUI classes expose `…Safely()` signals (for example `UpdateStateSafely`, `ShowSafely`,
`BindSafely`, `SetTranslatorSafely`) that these workers emit so Qt queues the work onto the GUI
thread. `QuickConnect` instead posts back through a mutex-guarded `Link` that `~Controller` clears,
so a late worker never touches a destroyed controller. Custom types crossing a queued connection
are registered with `qRegisterMetaType` in `ApdApplication::Prepare` and the `MainWindow`
constructor. The rule: `AGENTS.md` Critical Rules.

Shared primitives live in `Source/Helper.h`: `Helper::Timer`, `Helper::ConWorker`, `Helper::Callback`,
`Helper::Sides<T>` (a left/right pair), `Helper::Singleton`, `Helper::ToString<T>`.

spdlog is compiled in at trace level (`SPDLOG_ACTIVE_LEVEL`); `--trace` enables it at runtime.
Logging and assertion usage: `AGENTS.md` Conventions.

## Platform Abstraction

Windows code is isolated in three patterns:

- **Header switch** (`Bluetooth`, `GlobalMedia`): `Foo.h` includes `Foo_win.h` under
  `#if defined APD_OS_WIN`; `Foo_abstract.h` holds the pure-virtual or CRTP interface; `Foo_win.h`
  raises `#error` if compiled elsewhere.
- **Factory** (`AutoStart`): `Core::AutoStart::CreateAutoStartService()` returns an interface.
- **Injected backend** (`QuickConnect`): `ApdApplication` constructs the platform `Backend` and
  hands it to the controller, so tests can supply their own.

WinRT helpers live in `Source/Core/OS/Windows.h`.

## Translations

Locales are listed in `APD_TRANSLATION_LOCALES` in `CMakeLists.txt`; `.ts` files live in
`Source/Resource/Translation/`, and `.qm` files are generated into `Build/Binary/translations/`.

The `APD_CREATE_UPDATE_TS` target runs `lupdate -recursive Source/ -no-obsolete -locations none`;
normal builds only compile the existing `.ts` files to `.qm` (when to run it: `AGENTS.md`). Widgets
call `UTILS_QT_REGISTER_LANGUAGECHANGE` (`Source/Gui/Utils.h`) to retranslate live.

Adding a locale is a CMake edit plus a rebuild; see `CONTRIBUTING.md` for the translator workflow and
the `--print-all-locales` launch option.

## Updater

`Core::Update` queries the GitHub API for `APD_GITHUB_OWNER/APD_GITHUB_REPOSITORY` and accepts a
release only if its `html_url` starts with `Config::UrlReleases` (built from the same two values).
It picks the `.exe` asset whose name matches `CONFIG_CPACK_SYSTEM_NAME`, takes the expected SHA-256
from the asset's `sha256:` `digest`, and runs `VerifyFileSha256` on the download before executing
it. Qt 6 builds select `win64`; legacy clients select the `win32-bridge` alias. The release lookup
tries tags with and without the `v` prefix.

Versioning, tagging, signing, and draft releases: `Docs/Release.md`.

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

What may be committed for an animation: `AGENTS.md` Critical Rules. Existing animations are 360
frames at 60 fps.

`AirPods_4.avi` and `AirPods_4_ANC.avi` are currently byte-identical, so one conversion ran against
the wrong source. `AirPods_5` borrows `AirPods_4_ANC.avi`, so it shows the same artwork too.
Regenerating the ANC animation needs the original `AirPods1,5-v2` asset.
