<h1 align="center">
    <a href="https://github.com/SpriteOvO/AirPodsDesktop"><img src="/Source/Resource/Image/Icon.svg" alt="Icon" width="128"></a>
    <br>
    AirPodsDesktop
</h1>
<p align="center">提升 AirPods 在 Windows 上的使用体验</p>
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
<p align="center">🌎 <a href="/README.md">English</a> | 🌏 简体中文 | 🌏 <a href="/README-TW.md">繁體中文</a></p>

## 🔍 预览

![Preview Image](/Assets/Preview.gif)

## ✨ 特性

* 🔋 在通知区域查看电池信息。
* 👂 通过入耳检测自动控制媒体播放。
* 🚀 启用低延迟音频模式。
* 🌈 设备弹出窗口动画，以及浅色、深色或跟随系统的主题。

## 🛠️ 构建与测试

Windows 构建使用 C++20、CMake 3.20+、Visual Studio 2019、Qt 5.15.2（MSVC 2019 32 位），以及已完成初始化的 vcpkg。环境设置请参阅[构建说明](/Docs/Build.md)。

替换示例路径后，在仓库根目录运行以下命令：

```powershell
cmake -S . -B Build -G "Visual Studio 16 2019" -A Win32 `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -DCMAKE_PREFIX_PATH=C:\Qt\5.15.2\msvc2019 `
  -DAPD_BUILD_TESTS=ON
cmake --build Build --config RelWithDebInfo
ctest --test-dir Build -C RelWithDebInfo --output-on-failure
```

可执行文件与部署的 Qt 文件会输出至 `Build/Binary/`。添加 `-DAPD_ENABLE_CONSOLE=ON` 可启用控制台诊断；添加 `-DAPD_GENERATE_INSTALLER=ON` 可通过 NSIS 生成安装程序。

后台 CPU 使用率的改进与测量步骤请参阅[性能说明](/Docs/Performance.md)。

## 🤝 贡献

*AirPodsDesktop* 是一个开源项目，您可以通过以下方式贡献：

* [打开问题](https://github.com/SpriteOvO/AirPodsDesktop/issues/new/choose) 来报告错误或建议新功能。
* [提交 PR](https://github.com/SpriteOvO/AirPodsDesktop/compare) 来修复错误、改进文档或添加功能。
* [翻译到其他语言](/CONTRIBUTING.md#-translation-guide) 或 [改进现有的翻译](/CONTRIBUTING.md#-translation-guide)。

更新项目文档时，请保持 `README.md`、`README-CN.md` 与 `README-TW.md` 的章节顺序、功能、命令、链接及致谢内容一致。

## 💎 第三方依赖项

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

## 🍺 致谢

* [OpenPods](https://github.com/adolfintel/OpenPods)
* [Discontinued Privacy: Personal Data Leaks in Apple Bluetooth-Low-Energy Continuity Protocols](https://hal.inria.fr/hal-02394619/document)
* [MagicPods](https://magicpods.app/)
