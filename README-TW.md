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

![Preview Image](/Assets/Preview.gif)

## ✨ 功能

* 🔋 在通知區域查看電池資訊。
* 👂 透過入耳偵測自動控制媒體播放。
* 🚀 啟用低延遲音訊模式。
* 🌈 裝置彈出視窗動畫，以及淺色、深色或跟隨系統的主題。

## 🛠️ 建置與測試

Windows 建置使用 C++20、CMake 3.20+、Visual Studio 2019、Qt 5.15.2（MSVC 2019 32 位元），以及已完成初始化的 vcpkg。環境設定請參閱[建置說明](/Docs/Build.md)。

替換範例路徑後，在儲存庫根目錄執行下列命令：

```powershell
cmake -S . -B Build -G "Visual Studio 16 2019" -A Win32 `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -DCMAKE_PREFIX_PATH=C:\Qt\5.15.2\msvc2019 `
  -DAPD_BUILD_TESTS=ON
cmake --build Build --config RelWithDebInfo
ctest --test-dir Build -C RelWithDebInfo --output-on-failure
```

執行檔與部署的 Qt 檔案會輸出至 `Build/Binary/`。加入 `-DAPD_ENABLE_CONSOLE=ON` 可啟用主控台診斷；加入 `-DAPD_GENERATE_INSTALLER=ON` 可透過 NSIS 產生安裝程式。

背景 CPU 使用量的改善與量測步驟請參閱[效能說明](/Docs/Performance.md)。

## 🤝 貢獻

*AirPodsDesktop* 是一個開源項目，您可以透過以下方式貢獻：

* [開立問題](https://github.com/SpriteOvO/AirPodsDesktop/issues/new/choose) 以回報錯誤或建議新功能。
* [提交 PR](https://github.com/SpriteOvO/AirPodsDesktop/compare) 以修正錯誤、改善文件或新增功能。
* [翻譯成其它語言](/CONTRIBUTING.md#-translation-guide) 或 [改進現有的翻譯](/CONTRIBUTING.md#-translation-guide)。

更新專案文件時，請保持 `README.md`、`README-CN.md` 與 `README-TW.md` 的章節順序、功能、命令、連結及致謝內容一致。

## 💎 第三方相依項目

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

## 🍺 銘謝

* [OpenPods](https://github.com/adolfintel/OpenPods)
* [Discontinued Privacy: Personal Data Leaks in Apple Bluetooth-Low-Energy Continuity Protocols](https://hal.inria.fr/hal-02394619/document)
* [MagicPods](https://magicpods.app/)

### 貢獻者

* [@aizuon](https://github.com/aizuon) — 閒置 CPU 與低延遲音訊重構，提交於 [#199](https://github.com/SpriteOvO/AirPodsDesktop/pull/199)，經由 [#210](https://github.com/SpriteOvO/AirPodsDesktop/pull/210) 合併；另見[效能說明](/Docs/Performance.md)。
