# Build Instructions

## Linux prerequisites

AirPodsDesktop also builds for **Linux x64** and talks to the Bluetooth stack through BlueZ over D-Bus.
You need:

- CMake 3.20 or newer, and Ninja or Make.
- GCC 13 with C++20 support (the version this was verified with).
- Qt 6.4 or newer with the Widgets, Svg, Multimedia, and DBus modules. Qt 6.8.3 is the reference version.
- spdlog, cxxopts, nlohmann-json, magic_enum, and Boost (stacktrace, pfr). CMake uses the system
  packages when present and otherwise fetches them from GitHub, so the first configure needs network access.
- A running BlueZ (`bluetooth.service`) with a Bluetooth adapter.

At runtime, Qt 6.5 and later need `libxcb-cursor0` for the xcb platform plugin (Ubuntu 24.04 ships it as a separate package).

## Linux configure and build

```bash
git clone --recursive https://github.com/SpriteOvO/AirPodsDesktop.git
cd AirPodsDesktop
cmake -S . -B Build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/gcc_64 \
  -DAPD_BUILD_TESTS=ON
cmake --build Build
```

The executable is written to `Build/Binary/AirPodsDesktop`. `cmake --install Build` installs the
application together with the desktop entry from `Packaging/Linux/`.

## Linux portable bundle

`Packaging/Linux/make-bundle.sh` packages a build into a self-contained directory and a `.tar.gz`.
The bundle includes the Qt libraries and plugins the application loads, so the target machine does
not need Qt installed:

```bash
Packaging/Linux/make-bundle.sh Build /path/to/Qt/6.8.3/gcc_64 Dist [extra-lib-dir]
```

`extra-lib-dir` is optional and holds libraries Qt does not ship. Put `libxcb-cursor.so.0` there if
the build machine does not have it installed, since the Qt XCB plugin needs it. The bundle's `install.sh` adds the
application to the current user's menu without root.

## Debian package

`Packaging/Linux/make-deb.sh` wraps a bundle into a `.deb`. It installs the application under
`/opt/AirPodsDesktop`, exposes `/usr/bin/AirPodsDesktop`, adds the menu entry and icon, and declares
the system libraries it needs as dependencies:

```bash
Packaging/Linux/make-deb.sh Dist/AirPodsDesktop Dist
sudo apt install ./Dist/airpods-desktop_<version>_amd64.deb
```

## Windows prerequisites

AirPodsDesktop uses C++20 and builds for **Windows x64** with:

- [CMake](https://cmake.org/download/) 3.20 or newer.
- Visual Studio 2022 with the C++ desktop tools.
- A cloned and [bootstrapped vcpkg checkout](https://github.com/microsoft/vcpkg#quick-start-windows).
- [Qt 6.8.3](https://www.qt.io/download-qt-installer), including the `MSVC 2022 64-bit` and Qt Multimedia components.
- [NSIS](https://sourceforge.net/projects/nsis/files/latest/download) when generating an installer.

Qt 6.8.3 is the supported development, CI, and release baseline. It is installed from the public
Qt package feed, so repository builds do not require Qt account credentials.

## Configure and build

Clone the repository and enter its root directory in PowerShell:

```powershell
git clone --recursive https://github.com/SpriteOvO/AirPodsDesktop.git
cd AirPodsDesktop
```

Replace the vcpkg and Qt paths before configuring:

```powershell
cmake -S . -B Build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 `
  -DAPD_BUILD_TESTS=ON
cmake --build Build --config RelWithDebInfo
```

Win32, ARM64, Visual Studio 2019, and Qt 5 are not supported. Use a new build directory when
changing generators, architectures, or Qt installations. The minimum supported operating system is
Windows 10 version 1809 (build 17763); Windows 11 x64 is also supported.

Executables and deployed Qt files are written to `Build/Binary/`.
Useful configuration options are:

- `-DAPD_BUILD_TESTS=ON`: build and register the first-party tests (off by default).
- `-DAPD_ENABLE_CONSOLE=ON`: enable console diagnostics.
- `-DAPD_GENERATE_INSTALLER=ON`: generate an installer with NSIS.

See the `Build options` section in [CMakeLists.txt](/CMakeLists.txt) for all options.

## Run tests

After building with `APD_BUILD_TESTS=ON`, run from the repository root:

```powershell
ctest --test-dir Build -C RelWithDebInfo --output-on-failure
```

CTest runs the domain, quick-connect, animation, and UI rendering suites registered in
[Tests/CMakeLists.txt](/Tests/CMakeLists.txt). Animation and UI rendering tests need an
interactive Windows desktop; animation tests also use the Windows multimedia backend.
Qt runtime DLLs must be deployed beside the test executables or available on `PATH`.
