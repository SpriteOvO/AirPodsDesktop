# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

`AGENTS.md` holds the contributor-facing conventions (coding style, commit/PR guidelines). This file
adds the architecture and workflow detail that only emerges from reading across several files.

## Build & Test

Two supported targets: **Windows x64** (MSVC 2022, Qt 6.8.3, vcpkg) and **Linux x64** (GCC 13+,
Qt 6.8.3, BlueZ/D-Bus). The Linux port is working but not yet fully hardware-verified (see
[Linux port status](#linux-port-status)).

### Linux (Ubuntu 24.04)

Ubuntu ships Qt 6.4, so install Qt 6.8 separately. Full prerequisites and package lists are in
`Docs/Build.md`; the short version:

```bash
cmake -S . -B Build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH=~/.local/Qt/6.8.3/gcc_64 -DAPD_BUILD_TESTS=ON
cmake --build Build -j"$(nproc)"
ctest --test-dir Build --output-on-failure
```

Headless CI runs the suite under a virtual display:
`xvfb-run -a ctest --test-dir Build --output-on-failure`.

### Windows

Read `Docs/Build.md` before configuring. The 64-bit MSVC build needs a bootstrapped vcpkg checkout:

```powershell
cmake -S . -B Build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 -DAPD_BUILD_TESTS=ON
cmake --build Build --config RelWithDebInfo
ctest --test-dir Build -C RelWithDebInfo --output-on-failure
```

### Common commands

```bash
# Run one Qt Test slot directly (no .exe suffix on Linux).
./Build/Binary/AirPodsDomainTests MergesAdvertisementsFromBothSides

clang-format -i Source/Path/File.cpp
```

Binaries and deployed Qt files land in `Build/Binary/`.

With `APD_BUILD_TESTS=ON`, CTest registers `AirPodsDomainTests`, `QuickConnectTests`,
`AnimationViewTests`, and `UiRenderingTests`, plus `MainWindowRenderingScale*` and
`AirPods5RenderingScale*` variants that rerun two `UiRenderingTests` slots at `QT_SCALE_FACTOR`
1, 1.25, 1.5, and 2. Linux additionally builds and registers `GlobalMediaTests` and
`NoiseControlTests`.
`Tests/CMakeLists.txt` is the source of truth. CMake calls `enable_testing()` explicitly because
the fetched cxxopts can cache `BUILD_TESTING` off before `include(CTest)` runs.

The domain and quick-connect suites use `QCoreApplication`. Animation and UI rendering tests
create widgets and need an interactive desktop; on Linux CI provides one with `xvfb-run`.
Animation tests also exercise the platform multimedia backend. On Windows, Qt runtime DLLs must be
deployed beside the executables or be on `PATH`. The Windows CI job runs `UiRenderingTests.exe`
separately with a log file, then `ctest --exclude-regex '^UiRenderingTests$'`.

| CMake option | Default | Effect |
|---|---|---|
| `APD_BUILD_TESTS` | OFF | Builds `Tests/` and enables CTest |
| `APD_ENABLE_CONSOLE` | OFF | Windows console subsystem + `APD_ENABLE_CONSOLE` define |
| `APD_GENERATE_INSTALLER` | OFF | Windows: CPack/NSIS post-build. Linux: reruns `cpack` (DEB/TGZ) post-build |
| `APD_QT_DEPLOY` | ON | Windows `windeployqt` post-build |
| `APD_GITHUB_OWNER` / `APD_GITHUB_REPOSITORY` | `SpriteOvO` / `AirPodsDesktop` | Repository the updater checks; set these for fork builds |
| `APD_RELEASE_TAG_PREFIX` | `v` | Release tag prefix of the configured repository (empty for `1.2.3` tags) |
| `APD_BUILD_GIT_HASH` | — | Stamps the build hash into the log banner |

Compiler quirks baked into `CMakeLists.txt`: on MSVC, `CMAKE_CXX_STANDARD 20` is remapped to
`/std:c++latest` because `<format>` is unavailable under `/std:c++20`, and `/await` is forced
globally for cppwinrt. Both are load-bearing — the code uses `std::format` and WinRT. Linux uses
`BOOST_STACKTRACE_USE_ADDR2LINE` so stack traces need only `libdl`.

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
| `apd_core` | `AirPods::Manager`, `QuickConnect`, Linux `NoiseControl`, `Settings`, `SettingsRepository`, `Update`, `LowAudioLatency`, `AutoStart`, `Debug`, and the `_win.cpp`/`_linux.cpp` platform impls | `apd_domain`, Qt Multimedia, Qt DBus (Linux), cpr, nlohmann_json, boost::pfr |
| `apd_presentation` | `MainWindowPresentation` view models — plain state→struct mapping, no widgets | `apd_domain` only |
| `apd_gui` | `MainWindow`, `TrayIcon`, `TaskbarStatus`, `SelectWindow`, `UpdateWindow`, `SettingsWindow`, `Theme`, `AnimationPlayback`, `Widget::Battery`, `Widget::AnimationView` | `apd_core` + `apd_presentation`, Qt Widgets/Svg/Multimedia |
| `AirPodsDesktop` | `Main.cpp`, `Application.cpp`, resources | `apd_gui` + SingleApplication |

Domain and view-model tests link `apd_core` + `apd_presentation` + Qt Test and run without a
Bluetooth radio or a window. Animation and UI rendering tests additionally link `apd_gui`.
`Gui::TaskbarStatus` (the Windows taskbar battery dock) is compiled only on Windows; Linux gets an
interface-compatible no-op class defined inline in `TaskbarStatus.h`, which CMake still lists for
AUTOMOC.

## Runtime Data Flow

1. `Core::Bluetooth::AdvertisementWatcher` receives BLE advertisements: WinRT
   `BluetoothLEAdvertisementWatcher` on Windows (self-restarts 3s after failure), BlueZ over the
   system D-Bus on Linux (`SetDiscoveryFilter` with `Transport=le`, `DuplicateData=true`, then
   `PropertiesChanged` / `InterfacesAdded` parsing).
2. `AirPods::Manager::OnAdvertisementReceived` filters via `Details::Advertisement::IsDesiredAdv` —
   manufacturer data must carry Apple's vendor id `76` and pass `AppleCP::AirPods::IsValid`.
3. `Details::Advertisement` parses the fixed 27-byte `ProximityPairing` packet into an `AdvState`.
4. `Details::StateManager::OnAdvReceived` merges the two sides, applies the `rssi_min` floor, and
   arms the "lost" / per-side reset timers (10 s on Windows, 60 s on Linux, where BlueZ reports
   device-property changes far less often than WinRT delivers advertisements). It emits an
   `UpdateEvent { oldState, newState }`.
5. `Manager` raises Qt signals — `StateUpdated`, `Disconnected`, `LidToggled`,
   `ScannerAvailabilityChanged`, `BoundDeviceUnavailable`.
6. `ApdApplication::ConnectAirPodsManager()` is the single fan-out point: it wires those signals to
   `MainWindow`, `TrayIcon`, and `TaskbarStatus`. Look there first to trace any UI update.
   `ApdApplication` owns every manager and also implements `Settings::ApplyObserver`; its member
   declaration order is load-bearing (the quick-connect controller must outlive the tray icon and
   settings window that reference it), so keep that comment in `Application.h` true.
7. Ear detection: `Manager::OnBothInEar` calls `Core::GlobalMedia::Play()` / `Pause()` — Windows
   `GlobalSystemMediaTransportControls`, Linux MPRIS — when the setting is on.
8. `ApdApplication::Prepare` picks the platform quick-connect backend and registers
   `Core::AirPods::State` with `qRegisterMetaType`. `ApdApplication`'s constructor takes `argc` by
   reference because `QCoreApplication` keeps a reference to it for the whole process lifetime.

### Quick connect

`Core::QuickConnect` reconnects a paired Bluetooth audio endpoint when the user clicks the tray
icon (`tray_quick_connect_enabled` / `tray_quick_connect_device_id`). `Controller` owns the
policy and state machine (`Disabled`, `NoDevice`, `DeviceUnavailable`, `AlreadyConnected`,
`RequestStarted`, `Connected`, `Failed`, `TimedOut`); `Backend` is the platform seam.

- Windows: `WindowsBackend` enumerates `IMMDevice` endpoints through `IMMNotificationClient` and
  asks the driver to reconnect.
- Linux: `CreateLinuxBackend()` reads the BlueZ ObjectManager tree, calls `Device1.Connect`
  (20s D-Bus timeout), and watches `PropertiesChanged` for `Connected`. A timed-out `Connect`
  call keeps observing because the link may still come up.

The controller waits up to 10s for the endpoint to appear, and its workers post results back
through a mutex-protected `Link` rather than touching `Controller` directly. The reconnect worker
can sit in a blocking driver/D-Bus call, so `~Controller` detaches it instead of joining; anything
it still needs is held by `shared_ptr`. An expected outcome is a normal signal
(`OutcomeChanged`), not an exception or a dialog.

### Linux noise control

`Core::NoiseControl::Controller` owns the Linux tray menu's listening-mode state and uses the
bound classic Bluetooth address and resolved model, not the random BLE advertisement address or
Quick Connect's independently selected device. `ApdApplication` forwards bound-device lifecycle
updates with a binding generation; unknown or unsupported models do not start a control session.

`NoiseControl_linux.cpp` uses a nonblocking L2CAP socket (PSM `0x1001`) and the reverse-engineered
Apple Accessory Protocol. The controller runs the backend on a worker, rejects stale callbacks,
serializes replacement sessions and retries failures with bounded backoff. The tray checks a mode
only after a device report, not on a successful write. A request times out after eight seconds
without closing the session; a later report still updates the displayed mode. AAP reports carry no
request ID, so they cannot prove which request caused a report. Off Listening Mode may need enabling
on an Apple device; the app does not silently change that separate setting. See `Docs/Build.md` and
`Docs/NoiseControl-Licenses.txt` for limitations and protocol-reference attribution.

`NoiseControlTests` covers protocol packets, receive ordering, timeouts and controller lifecycle
with fakes; it cannot prove a particular AirPods model or firmware works. Trace-level noise-control
diagnostics record modes, timing and request serials without raw packets or device addresses, but
other application trace messages can contain Bluetooth-related identifiers and advertisement data.

### Theme, autostart, updater, and other platform behaviour

- `Gui::Theme` reads light/dark and the accent colour from the Windows personalization APIs or the
  freedesktop `org.freedesktop.portal.Settings` portal (`color-scheme`, `accent-color`), falls back
  to `QStyleHints`, subscribes to portal `SettingChanged`, and debounces refresh by 150ms.
- `Core::AutoStart` uses `CreateAutoStartService()`: a `QSettings` `Registry64Format` value on
  Windows, an XDG `autostart/AirPodsDesktop.desktop` entry on Linux.
- `Core::Update::AsyncChecker` is a no-op off Windows, because releases only carry Windows
  installers and every asset would be filtered out.

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
3. A user-visible setting also needs a control in `SettingsWindow.ui`, wiring in
   `SettingsWindow.cpp`, and a translation string — the metadata only supplies name/default/description.

`ApplyObserver` exists so `apd_core` never includes GUI headers — Core raises the intent, the
application layer performs it. Persistence goes through `Core::Settings::Repository`
(`QSettings`-backed in production, `MemoryRepository` injected via `SetRepository()` in tests).

Two rules with teeth:

- Never delete an obsolete field — mark it `Impl::Deprecated()` and leave it in place.
- Bump `kFieldsAbiVersion` when a key's name or type changes incompatibly. A mismatch makes
  `Settings::Load()` return `AbiIncompatible`, which drops the user into the first-time-use wizard.

`ModifiableAccess()` returns an RAII accessor holding the settings mutex; it saves and applies the
diff in its destructor, so mutate through it rather than calling `Save()` by hand. On 64-bit Linux
`uint64_t` fields are written as `qulonglong`, because `QVariant` has no unambiguous unsigned
64-bit constructor on LP64.

## Presentation Layer

`Gui::MainWindowViewModel` (in `apd_presentation`) consumes `Core::AirPods::State` plus lifecycle
calls (`Available`/`Unavailable`/`Disconnect`/`Bind`/`Unbind`) and produces a plain comparable
`MainWindowPresentation` struct. `MainWindow` only renders that struct. New main-window display logic
belongs in the view model, where `Tests/AirPodsDomainTests.cpp` can cover it without a widget.

`GetAnimationPresentation(Model)` maps a model to its `qrc:/Resource/Video/*.avi` path and source size.

## Threading & Qt Conventions

BLE callbacks, the device-lookup `std::jthread`, the quick-connect refresh/reconnect workers, and
the hourly `Update::AsyncChecker` run off the GUI thread. GUI classes therefore expose
`…Safely()` signals — `UpdateStateSafely`, `ShowSafely`, `HideSafely`, `BindSafely`,
`SetTranslatorSafely`, `ControlSafely` — that worker threads emit so Qt queues the work onto the
GUI thread. **Never touch a widget directly from a worker thread; emit the `*Safely` signal
instead.** Custom types crossing a queued connection need `qRegisterMetaType` (see
`Core::AirPods::State` in `ApdApplication::Prepare`).

Linux D-Bus work is split accordingly: BlueZ signal handlers live in `QObject` notifiers that queue
back to their owner, blocking calls (theme reads, `GetManagedObjects`, `Device1.Connect`) happen on
worker threads or with short timeouts, and `Bluetooth_linux` marshals `a{sa{sv}}` replies by hand
because QtDBus leaves nested values opaque.

Shared primitives live in `Source/Helper.h`: `Helper::Timer`, `Helper::ConWorker`, `Helper::Callback`,
`Helper::Sides<T>` (a left/right pair), `Helper::Singleton`, `Helper::ToString<T>`. `Helper::Timer`
and `ConWorker` skip `join()` when stopped from their own callback thread.

Logging is `LOG(Info, "fmt {}", args)` (spdlog, compiled in at trace level; `--trace` enables it at
runtime). Add a `Helper::ToString<T>` specialization to make a domain type loggable. Failures use
`APD_ASSERT(cond)` and `FatalError(msg, report)`; off Windows a fatal error is printed to stderr
because there is no non-GUI-thread-safe native dialog.

## Platform Abstraction

Platform code is isolated by a consistent file convention:

- `Foo.h` — public header; includes `Foo_win.h` or `Foo_linux.h` under `#if defined APD_OS_WIN` /
  `APD_OS_LINUX`.
- `Foo_abstract.h` — pure-virtual or CRTP interface shared by all platforms.
- `Foo_win.h` / `Foo_win.cpp`, `Foo_linux.h` / `Foo_linux.cpp` — platform implementation, each
  guarded by `#error` if compiled elsewhere.

Used by `Bluetooth`, `GlobalMedia`, `QuickConnect`, `AutoStart`, and `Theme` (theme is a single
class with platform branches). `QuickConnect` declares its `Backend` interface in `QuickConnect.h`
and exposes `WindowsBackend` / `CreateLinuxBackend()` from the platform headers. Linux-only
`NoiseControl` declares its backend in `NoiseControl.h`; its controller and implementation are
compiled only on Linux. `AutoStart` uses a `CreateAutoStartService()` factory. Windows `_win.cpp`
files are listed explicitly under `if (WIN32)` in `CMakeLists.txt`. Linux collects the names in
`APD_LINUX_PLATFORM_CODE_FILES` when they exist; a new Linux platform file must be added to that
list. `TaskbarStatus` uses the inverse pattern: a real Windows implementation plus a no-op header
on other platforms.

`Core::Bluez::GetManagedObjects()` in `Source/Core/BluezObjectManager.h` is the shared BlueZ
ObjectManager reader; BlueZ-specific constants live beside it. WinRT helpers live in
`Source/Core/OS/Windows.h`.

## Translations

Locales are listed in `APD_TRANSLATION_LOCALES` in `CMakeLists.txt`; `.ts` files live in
`Source/Resource/Translation/`, and `.qm` files are generated into `Build/Binary/translations/`.

A custom `APD_CREATE_UPDATE_TS` target runs `lupdate -recursive Source/ -no-obsolete -locations none`
only when requested. Normal builds compile the existing `.ts` files to `.qm` without rewriting
tracked translations. Widgets call `UTILS_QT_REGISTER_LANGUAGECHANGE` to retranslate live.
`ApdApplication::SetTranslator` loads `.qm` files from `translations/` beside the executable, then
from each `GenericDataLocation/airpodsdesktop/translations` (the installed Linux path).

Adding a locale is a CMake edit plus a rebuild; see `CONTRIBUTING.md` for the translator workflow and
the `--print-all-locales` launch option.

## Packaging & Release

**Windows:** `APD_GENERATE_INSTALLER=ON` runs CPack/NSIS post-build; the `.exe` installer and a
portable tree land in `Build/Installer/`. `APD_QT_DEPLOY` runs `windeployqt` and copies the MSVC
runtime.

**Linux:** the install rules produce a relocatable bundle under `lib/airpodsdesktop` with the Qt
runtime closure (`CMake/BundleLinuxRuntime.cmake` copies only what the executable and selected
plugins load), `qt.conf`, `$ORIGIN` runpaths, translations, licenses, a desktop entry, and an icon.
`Packaging/airpodsdesktop` is the launcher installed to `bin`. `cpack` (or
`APD_GENERATE_INSTALLER=ON`) produces a `.deb` and `.tar.gz` in `Build/Installer/`;
`cmake --install Build --prefix ~/.local --component AirPodsDesktop` installs the same tree.
`dpkg-shlibdeps` generates dependencies from the build machine, so a package built on Ubuntu 24.04
names the `t64` packages and will not install on older releases.

`Core::Update` queries the GitHub API for `APD_GITHUB_OWNER/APD_GITHUB_REPOSITORY`, rejects metadata
from any other repository, selects the `.exe` release asset matching the compiled CPack architecture,
and verifies its SHA-256 against the release digest before executing it. `AsyncChecker` polls hourly
on Windows only. Qt 6 builds select `win64`; legacy clients select the signed `win32-bridge` alias,
whose installer checks for AMD64 before replacing anything.

The version must be identical in `project(... VERSION ...)` in `CMakeLists.txt` and `version-string`
in `vcpkg.json`; a `v*.*.*` tag must match both (pre-0.5.0 tags omit the `v`). CI fails the build
otherwise. Forks must also set `APD_GITHUB_OWNER`, `APD_GITHUB_REPOSITORY`, and, when their tags do
not start with `v`, `APD_RELEASE_TAG_PREFIX`. Full process is in `Docs/Release.md`.

## Linux port status

`LINUX_PORT_PLAN.md` is the original plan and status log. Working today: build/startup, BLE scanning
through BlueZ, tray battery display, MPRIS ear detection, BlueZ quick connect, XDG autostart, and
Linux noise control from the tray. On 2026-11-13, Noise Cancellation and Adaptive requests were
confirmed by device reports on AirPods Pro 3; the user also reported Transparency and Off working,
but those two later selections were not corroborated in the trace. Earlier Off and repeat-Adaptive
requests timed out, so broader hardware and firmware verification is still needed.

Known gaps include GNOME's tray needing the AppIndicator extension, no Linux release assets so
updates are disabled, and the taskbar battery dock being Windows-only. Treat changes to
`Source/Core/*_linux.cpp` as needing a Linux build and, where possible, a real adapter.

## Adding an AirPods Model

1. Add the enum value to `Core::AirPods::Model` (before `_Max`) and a case to
   `Helper::ToString<Model>` — both in `Source/Core/Base.h`.
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
