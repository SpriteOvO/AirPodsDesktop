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

| Light theme | Dark theme |
| :---: | :---: |
| <img src="/Assets/Preview.gif" alt="Light theme popup animation" width="360"> | <img src="/Assets/Preview-Dark.gif" alt="Dark theme popup animation" width="360"> |

## ✨ Features

* 🔋 Battery information in the notification area.
* 👂 Automatic ear detection and media playback control.
* 🚀 Low audio latency mode.
* 🌈 Animated device popups and light, dark, or system themes.

## 💻 System requirements

Official builds support Windows 10 1809 or newer and Windows 11 on x64. 32-bit Windows and ARM64
are not supported. Existing Qt 5/Win32 users can migrate through the in-app updater while keeping
their user settings.

## 🛠️ Build and test

See the [Build Instructions](/Docs/Build.md) for Windows prerequisites, build commands, and tests.

## 🤝 Contribute

*AirPodsDesktop* is an open source project, here are some ways you can contribute:

* [Open an issue](https://github.com/SpriteOvO/AirPodsDesktop/issues/new/choose) to report bugs or suggest new features.
* [Submit a PR](https://github.com/SpriteOvO/AirPodsDesktop/compare) to fix bugs, improve documentation, or add features.
* [Translate to other languages](/CONTRIBUTING.md#-translation-guide) or [improve existing translations](/CONTRIBUTING.md#-translation-guide).

Keep `README.md`, `README-CN.md`, and `README-TW.md` aligned in section order, features, commands, links, and credits when updating project documentation.

## 💎 Third-party dependencies

* [Qt 6.8.3](https://www.qt.io/download-qt-installer) ([LGPLv3 License](https://doc.qt.io/qt-6/lgpl.html))
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
