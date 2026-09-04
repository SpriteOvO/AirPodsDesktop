# Build Instructions

## Windows prerequisites

AirPodsDesktop uses C++20 and builds for **Win32 (32-bit)** with:

- [CMake](https://cmake.org/download/) 3.20 or newer.
- Visual Studio 2019 with the C++ desktop tools, or Visual Studio 2022 with the corresponding generator.
- A cloned and [bootstrapped vcpkg checkout](https://github.com/microsoft/vcpkg#quick-start-windows).
- [Qt 5.15.2](https://www.qt.io/download-qt-installer), including the `MSVC 2019 32-bit` components.
- [NSIS](https://sourceforge.net/projects/nsis/files/latest/download) when generating an installer.

## Configure and build

Clone the repository and enter its root directory in PowerShell:

```powershell
git clone --recursive https://github.com/SpriteOvO/AirPodsDesktop.git
cd AirPodsDesktop
```

Replace the vcpkg and Qt paths before configuring:

```powershell
cmake -S . -B Build -G "Visual Studio 16 2019" -A Win32 `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -DCMAKE_PREFIX_PATH=C:\Qt\5.15.2\msvc2019 `
  -DAPD_BUILD_TESTS=ON
cmake --build Build --config RelWithDebInfo
```

For Visual Studio 2022, use `-G "Visual Studio 17 2022"`. Keep `-A Win32` and the
32-bit Qt package. Use a new build directory when changing generators.

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

For manual update-dialog testing with simulated downloads, see
[Update UI testing](/Docs/UpdateUiTesting.md).
