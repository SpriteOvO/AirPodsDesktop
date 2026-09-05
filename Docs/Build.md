# Build Instructions

## Windows prerequisites

AirPodsDesktop uses C++20 and builds for **Windows x64** with:

- [CMake](https://cmake.org/download/) 3.20 or newer.
- Visual Studio 2022 with the C++ desktop tools.
- A cloned and [bootstrapped vcpkg checkout](https://github.com/microsoft/vcpkg#quick-start-windows).
- [Qt 6.8.4](https://www.qt.io/download-qt-installer), including the `MSVC 2022 64-bit` and Qt Multimedia components.
- [NSIS](https://sourceforge.net/projects/nsis/files/latest/download) when generating an installer.

Qt 6.8.4 is distributed through Qt's authenticated online installer. Repository CI therefore needs
`QT_EMAIL` and `QT_PW` secrets for a Qt account that can install the 6.8.4 MSVC 2022 package. Since
fork pull requests cannot access those secrets, the workflow uses public Qt 6.8.3 only for that
untrusted compatibility build; official branch and release builds remain pinned to 6.8.4.

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
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.4\msvc2022_64 `
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
