<h1 align="center">
    <a href="https://github.com/SpriteOvO/AirPodsDesktop"><img src="/Source/Resource/Image/Icon.svg" alt="Icon" width="128"></a>
    <br>
    AirPodsDesktop
</h1>
<p align="center">Enhance your AirPods experience on Windows</p>
<p align="center">
    <a href="https://github.com/SpriteOvO/AirPodsDesktop/actions/workflows/windows.yml">
        <img src="https://github.com/SpriteOvO/AirPodsDesktop/actions/workflows/windows.yml/badge.svg"/>
    </a>
    <a href="https://github.com/SpriteOvO/AirPodsDesktop/releases">
        <img src="https://img.shields.io/github/v/release/SpriteOvO/AirPodsDesktop?include_prereleases"/>
    </a>
    <a href="https://github.com/SpriteOvO/AirPodsDesktop/releases">
        <img src="https://img.shields.io/github/downloads/SpriteOvO/AirPodsDesktop/total.svg"/>
    </a>
    <a href="https://github.com/SpriteOvO/AirPodsDesktop/compare">
        <img src="https://img.shields.io/badge/PRs-welcome-brightgreen.svg"/>
    </a>
    <a href="/LICENSE">
        <img src="https://img.shields.io/badge/license-GPLv3-yellow.svg"/>
    </a>
</p>
<p align="center">🌎 English | 🌏 <a href="/README-CN.md">简体中文</a> | 🌏 <a href="/README-TW.md">繁體中文</a></p>

## 🔍 Preview

![Preview Image](/Assets/Preview.gif)

## ✨ Features

* 🔋 View battery information from the notification area.
* 👂 Automatically control media playback with ear detection.
* 🚀 Enable low audio latency mode.
* 🌈 Enjoy animated device popups and light, dark, or system-following themes.

## 🛠️ Build and test

The Windows build uses C++20, CMake 3.20+, Visual Studio 2019, Qt 5.15.2 (MSVC 2019 32-bit), and a bootstrapped vcpkg checkout. See the [Build Instructions](/Docs/Build.md) for setup.

Run these commands from the repository root after replacing the example paths:

```powershell
cmake -S . -B Build -G "Visual Studio 16 2019" -A Win32 `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -DCMAKE_PREFIX_PATH=C:\Qt\5.15.2\msvc2019 `
  -DAPD_BUILD_TESTS=ON
cmake --build Build --config RelWithDebInfo
ctest --test-dir Build -C RelWithDebInfo --output-on-failure
```

Executables and deployed Qt files are written to `Build/Binary/`. Add `-DAPD_ENABLE_CONSOLE=ON` for console diagnostics or `-DAPD_GENERATE_INSTALLER=ON` to generate an installer with NSIS.

## 🤝 Contribute

*AirPodsDesktop* is an open source project, here are some ways you can contribute:

* [Open an issue](https://github.com/SpriteOvO/AirPodsDesktop/issues/new/choose) to report bugs or suggest new features.
* [Submit a PR](https://github.com/SpriteOvO/AirPodsDesktop/compare) to fix bugs, improve documentation, or add features.
* [Translate to other languages](/CONTRIBUTING.md#-translation-guide) or [improve existing translations](/CONTRIBUTING.md#-translation-guide).

Keep `README.md`, `README-CN.md`, and `README-TW.md` aligned in section order, features, commands, links, and credits when updating project documentation.

## 💎 Third-party dependencies

* [Qt 5.15.2](https://www.qt.io/download-qt-installer) ([LGPLv3 License](https://doc.qt.io/qt-5/lgpl.html))
* [spdlog](https://github.com/gabime/spdlog) ([MIT License](https://github.com/gabime/spdlog/blob/v1.x/LICENSE))
* [cxxopts](https://github.com/jarro2783/cxxopts) ([MIT License](https://github.com/jarro2783/cxxopts/blob/master/LICENSE))
* [cpr](https://github.com/whoshuu/cpr) ([MIT License](https://github.com/whoshuu/cpr/blob/master/LICENSE))
* [json](https://github.com/nlohmann/json) ([MIT License](https://github.com/nlohmann/json/blob/develop/LICENSE.MIT))
* [SingleApplication](https://github.com/itay-grudev/SingleApplication) ([MIT License](https://github.com/itay-grudev/SingleApplication/blob/master/LICENSE))
* [pfr](https://github.com/boostorg/pfr) ([BSL-1.0 License](https://github.com/boostorg/pfr/blob/develop/LICENSE_1_0.txt))
* [magic_enum](https://github.com/Neargye/magic_enum) ([MIT License](https://github.com/Neargye/magic_enum/blob/master/LICENSE))
* [stacktrace](https://github.com/boostorg/stacktrace) ([BSL-1.0 License](https://www.boost.org/LICENSE_1_0.txt))
* [Inter](/Source/Resource/Font/Inter/LICENSE.txt) (SIL Open Font License 1.1)
* [Noto Sans TC](/Source/Resource/Font/NotoSansTC/LICENSE.txt) (SIL Open Font License 1.1)

## 🍺 Credits

* [OpenPods](https://github.com/adolfintel/OpenPods)
* [Discontinued Privacy: Personal Data Leaks in Apple Bluetooth-Low-Energy Continuity Protocols](https://hal.inria.fr/hal-02394619/document)
* [MagicPods](https://magicpods.app/)
