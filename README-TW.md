<h1 align="center">
    <a href="https://github.com/SpriteOvO/AirPodsDesktop"><img src="/Source/Resource/Image/Icon.svg" alt="Icon" width="128"></a>
    <br>
    AirPodsDesktop
</h1>
<p align="center">提升 AirPods 在 Windows 上的使用體驗</p>
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
<p align="center">🌎 <a href="/README.md">English</a> | 🌏 <a href="/README-CN.md">简体中文</a> | 🌏 繁體中文</p>

## 🔍 預覽

| 淺色主題 | 深色主題 |
| :---: | :---: |
| <img src="/Assets/Preview.gif" alt="淺色主題彈窗動畫" width="360"> | <img src="/Assets/Preview-Dark.gif" alt="深色主題彈窗動畫" width="360"> |

## ✨ 功能

* 🔋 通知區域電池資訊。
* 👂 自動入耳偵測與媒體播放控制。
* 🚀 低延遲音訊模式。
* 🌈 裝置彈出視窗動畫，以及淺色、深色或系統主題。

## 💻 系統需求

正式版本支援 Windows 10 1809 以上或 Windows 11 x64，不支援 32 位元 Windows 與 ARM64。
現有 Qt 5／Win32 用戶可直接使用程式內更新器遷移至 Qt 6／x64，既有使用者設定會保留。

## 🛠️ 建置與測試

Windows 環境需求、建置命令與測試方式請參閱[建置說明](/Docs/Build.md)。

## 🤝 貢獻

*AirPodsDesktop* 是一個開源項目，您可以透過以下方式貢獻：

* [開立問題](https://github.com/SpriteOvO/AirPodsDesktop/issues/new/choose) 以回報錯誤或建議新功能。
* [提交 PR](https://github.com/SpriteOvO/AirPodsDesktop/compare) 以修正錯誤、改善文件或新增功能。
* [翻譯成其它語言](/CONTRIBUTING.md#-translation-guide) 或 [改進現有的翻譯](/CONTRIBUTING.md#-translation-guide)。

更新專案文件時，請保持 `README.md`、`README-CN.md` 與 `README-TW.md` 的章節順序、功能、命令、連結及致謝內容一致。

## 💎 第三方相依項目

* [Qt 6.8.4](https://www.qt.io/download-qt-installer) ([LGPLv3 License](https://doc.qt.io/qt-6/lgpl.html))
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

## 🍺 銘謝

* [OpenPods](https://github.com/adolfintel/OpenPods)
* [Discontinued Privacy: Personal Data Leaks in Apple Bluetooth-Low-Energy Continuity Protocols](https://hal.inria.fr/hal-02394619/document)
* [MagicPods](https://magicpods.app/)
