# Build Instructions

## Linux (Ubuntu 24.04)

AirPodsDesktop builds and runs on 64-bit Linux. The platform implementations live in the
`*_linux.cpp` files under `Source/Core/` and talk to BlueZ over D-Bus; ear detection drives the
desktop's MPRIS players, and settings and autostart use the XDG directories.

Prerequisites:

- CMake 3.20 or newer and a C++20 compiler (GCC 13 or newer).
- Qt 6.8 with Core, Gui, Widgets, Svg, Multimedia and LinguistTools. Ubuntu 24.04 ships Qt 6.4, so
  install Qt 6.8 separately, for example with `aqtinstall`:

  ```bash
  uv tool run --from aqtinstall aqt install-qt linux desktop 6.8.3 linux_gcc_64 \
    -O ~/.local/Qt --modules qtmultimedia
  ```

  `qtsvg` and `qttools` are part of the base Linux desktop package; passing them as modules
  makes aqtinstall fail because it cannot locate them as separate archives.

- System headers:

  ```bash
  sudo apt install build-essential cmake libssl-dev zlib1g-dev libboost-dev \
    libbluetooth-dev libdbus-1-dev libglib2.0-dev libpipewire-0.3-dev libpulse-dev \
    libgl-dev libglvnd-dev libglx-dev libopengl-dev libegl-dev
  ```

Configure and build:

```bash
cmake -S . -B Build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH=~/.local/Qt/6.8.3/gcc_64
cmake --build Build -j"$(nproc)"
```

The Linux platform files are added to `apd_core` automatically as they appear in the source tree.
Current Linux status: the application builds and starts, scans BLE advertisements through BlueZ,
shows battery status in the tray, controls MPRIS players for ear detection, connects or disconnects
the device explicitly selected for Quick Connect through BlueZ, and writes an XDG autostart entry.
The Linux tray menu has separate Connect and Disconnect selected device actions; enable Quick Connect
and select a paired audio device in Settings first. This selection is independent of the AirPods
binding used for battery display and noise control. Disconnect calls BlueZ Device1.Disconnect, which
can disconnect all profiles for that device. Update checks are
disabled because releases only carry Windows installers, and the taskbar battery dock is
Windows-only. See [Package for Linux](#package-for-linux) to build a `.deb` or `.tar.gz`.

Linux noise control uses the bound classic Bluetooth device and requires BlueZ to expose a
recognized product ID through that device's `Modalias`. If that optional property is absent or
unrecognized, the tray shows noise control as unsupported or unknown and does not open an AAP
socket. BLE advertisements are not used to guess the bound device's model. AAP support and
handshake behavior still require real-device verification.

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
[Tests/CMakeLists.txt](/Tests/CMakeLists.txt). On Linux, drop the `-C RelWithDebInfo` argument and
run `ctest --test-dir Build --output-on-failure`.

The animation and UI rendering suites create widgets, so they need an interactive desktop session
and the multimedia backend the platform provides. On Windows, Qt runtime DLLs must be deployed
beside the test executables or available on `PATH`.

## Package for Linux

The Linux install rules produce a relocatable bundle: the executable with a private copy of the Qt
runtime in `lib/airpodsdesktop` (only the libraries the executable and its plugins actually load),
translations, licenses, a desktop entry, and the icon. `cpack` turns that tree into a `.deb` and a
`.tar.gz` in `Build/Installer/`:

```bash
cmake --build Build -j"$(nproc)"
cd Build && cpack
```

`cmake --install Build --prefix ~/.local --component AirPodsDesktop` installs the same tree for the
current user, and `~/.local/bin/airpodsdesktop` launches it from any prefix. The component selects
only the application; the fetched third-party libraries have install rules of their own. The bundle
carries its own Qt (`qt.conf` plus `$ORIGIN` runpaths), so no Qt installation is needed on the
target machine.

The `.deb` dependency list is generated with `dpkg-shlibdeps` from the build machine, so a package
built on Ubuntu 24.04 names the `t64` library packages and will not install on older releases. Build
distribution packages with `-DCMAKE_BUILD_TYPE=Release`; the install step strips the executable
unless the build type is `Debug`.
