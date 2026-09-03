# AirPodsDesktop：Qt 6 遷移評估

> 評估日期：2026-09-01
> 評估範圍：Windows、C++20、Qt Widgets、CMake、目前儲存庫的靜態盤點
> 資料原則：Qt 相關事實只引用 Qt 官方文件、Qt Wiki 或 Qt 官方部落格；對本專案的工期、風險與效能判斷則明確標示為「本專案推估」。

## 結論先行

建議升級 Qt 6，但不要把它當成單純的框架版本更新，也不要期待僅靠換版就讓 Widgets UI 大幅變快。對 AirPodsDesktop 而言，這是一個「**Win32/x86 → x64、Qt 5 Multimedia → Qt 6 Multimedia、發布鏈現代化**」的聯合遷移。

截至評估日，建議的實際目標是：

- **開源／社群維護路線：Qt 6.11.2、MSVC 2022、x86_64。** Qt 官方目前列出的最新受支援版本為 Qt 6.11.2，標準支援至 2027-03-17；Qt 6.11 的 Windows 支援矩陣為 Windows 10 1809+ 或 Windows 11、`x86_64`、MSVC 2022（另支援 MinGW-w64），不包含 x86/Win32。[Qt Releases](https://doc.qt.io/qt-6/qt-releases.html)；[Qt for Windows](https://doc.qt.io/qt-6/windows.html)
- **商業長期維護路線：Qt 6.8 LTS、MSVC 2022、x86_64。** Qt 6.8 起 LTS 商業支援為五年，Qt 6.8 標準支援至 2029-10-08；但延伸 LTS patch 的即時存取限商業客戶，不能把「6.8 LTS」誤解成開源使用者會自動獲得五年相同維護。[Qt Releases](https://doc.qt.io/qt-6/qt-releases.html)；[Qt 6.8 LTS Released](https://www.qt.io/blog/qt-6.8-released)
- **不建議以 Qt 6.5 或更早版本作為新遷移目標。** Qt 6.5 已結束標準支援；Qt 官方也建議新開發升到最新 Qt 6，或在有商業 LTS 需求時採 Qt 6.8。[Qt 6.5 Reaches End-of-Support](https://www.qt.io/blog/qt-6.5-reaches-end-of-support)
- **若產品硬性要求繼續發布 32 位元 Windows 版，就沒有官方支援矩陣內的 Qt 6 路徑。** 自行編譯 x86 Qt 6 即使技術上可能成功，也不屬 Qt 6.11 官方列出的 Windows 支援組合，因此不應作為正式產品基線。[Qt for Windows](https://doc.qt.io/qt-6/windows.html)

整體建議為「**先把現有 Qt 5.15.2 版本移到 x64，再建立 Qt 5/Qt 6 雙建置，最後切換 Qt 6**」。這能把架構、框架 API、Multimedia 後端與安裝更新格式的風險拆開驗證。

## 目前專案基線

### Qt 與建置使用面

儲存庫目前明確使用：

- CMake 3.20+、C++20、Visual Studio 2019 文件基線；CI 已用 Visual Studio 2022 generator，但仍指定 `-A Win32`。
- Qt 5.15.2 `msvc2019` 32 位元套件。
- Qt 模組：`Core`、`Gui`、`Widgets`、`Svg`、`Multimedia`、`MultimediaWidgets`、`LinguistTools`、`Test`。
- Widgets `.ui`、QPainter 自繪電池與系統匣圖示、QSvgRenderer、QSystemTrayIcon、QTranslator 與 `.ts/.qm` 翻譯。
- 主視窗以 `QMediaPlayer + QVideoWidget` 播放嵌入資源的 AVI 動畫；低音訊延遲功能以 `QMediaPlayer + QMediaPlaylist` 循環播放靜音 MP3。
- Bluetooth 掃描、裝置查詢與全域媒體控制不是 Qt Bluetooth/WinExtras，而是專案自己的 C++/WinRT 與 Win32 實作。

這表示遷移不需要重寫 AirPods domain model；主要修改會集中在 `CMakeLists.txt`、`Source/Core/LowAudioLatency.*`、`Source/Gui/MainWindow.*`、少數 Qt 6 已淘汰 API，以及 CI／NSIS／updater 的 x64 發布假設。

### 已識別的直接熱點

| 現況 | Qt 6 影響 | 預期處理 |
|---|---|---|
| `find_package(Qt5 ...)`、`Qt5::...` | Qt 6 target 名稱與工具命令不同 | 先改為 Qt 5.15/6 可共用的 versionless CMake 形式，再切 Qt 6 |
| `Qt5LinguistTools`、手動呼叫 `Qt5_LUPDATE_EXECUTABLE`、`qt5_add_translation` | Qt 6 提供 `LinguistTools` 與 `qt_add_translations` | 以 Qt 6 翻譯 CMake API 取代自製 target，保留既有 `.ts` 檔名與外部 `translations/` 目錄契約 |
| `QAudioDeviceInfo` | Qt 6 以 `QMediaDevices`／`QAudioDevice` 取代 | 用 `QMediaDevices::audioOutputs()` 或 `defaultAudioOutput()` 檢查裝置 |
| `QMediaPlaylist` | Qt 6 已移除 | 單一靜音檔直接用 `QMediaPlayer::setLoops(QMediaPlayer::Infinite)`，不需 playlist |
| `QMediaContent`、`setMedia()` | Qt 6 已移除／改為 URL source | 改用 `setSource(QUrl)`，清空時用空 `QUrl` |
| `QMediaPlayer::State/stateChanged` | Qt 6 改為 `PlaybackState/playbackStateChanged` | 更新型別與 signal；動畫可直接用 `setLoops(Infinite)`，移除手動停止後重播 |
| `QMediaPlayer::setMuted()` | Qt 6 的音訊輸出由 `QAudioOutput` 明確連接 | 純影片動畫可不連音訊輸出；靜音 MP3 功能則建立並連接 `QAudioOutput` |
| `#include <QDesktopWidget>` | `QDesktopWidget` 在 Qt 6 移除 | 此 include 目前看似未使用，移除；螢幕幾何統一使用 `QScreen` |
| `QMouseEvent::globalPos()` | Qt 6.0 起 deprecated | 改用 `globalPosition().toPoint()`；雙版本期間可包一層相容 helper |
| `Qt::AA_EnableHighDpiScaling` | Qt 6 高 DPI 永遠啟用 | Qt 6 build 移除該 attribute；保留並測試 rounding policy |
| `Qt5_DIR/../../../bin/windeployqt` | 綁死 Qt 5 安裝結構 | 改用 Qt 6 CMake deployment API 或至少由 Qt tool target/環境取得工具 |
| CI／安裝檔／測試資料中的 `win32` | 官方 Qt 6 Windows 不支援 x86 | 全面改為 x64，並設計舊版 x86 updater 的過渡策略 |

## 升級效益

### 1. 回到受支援的平台與安全維護線

目前專案固定在 Qt 5.15.2。Qt 官方版本表顯示 Qt 5.15 的一般支援早已結束；即使列出的最新 5.15 LTS 是 5.15.19，其訂閱授權標準支援也在 2025-05-26 結束，之後僅有額外的 Extended Security Maintenance。相較之下，Qt 6.11 仍在標準支援期，而商業 Qt 6.8 LTS 可維護至 2029 年。[Qt Releases](https://doc.qt.io/qt-6/qt-releases.html)

因此，Qt 6 的最大確定效益不是跑分，而是：

- 有現行 patch、平台修正與安全更新來源。
- 與目前受支援的 MSVC 2022、Windows 10/11 x64 組合對齊。
- 不再持續承擔 Qt 5.15.2 舊版中的已知缺陷與部署工具老化。

專案採 GPLv3，Qt 6 的 Core/Gui/Widgets/Multimedia/SVG 等使用面可依其開源授權條款使用；Qt 官方仍要求逐一確認模組與第三方元件的授權，且自 Qt 6.8 起提供 SPDX SBOM。[Qt Licensing](https://doc.qt.io/qt-6/licensing.html)

### 2. Windows x64 與工具鏈現代化

Qt 6.11 官方支援的是 Windows 10 1809+／Windows 11 的 `x86_64 + MSVC 2022`。把 AirPodsDesktop 從 Win32 移到 x64，可消除 32 位元位址空間限制並與 Qt 的官方 binary、現行 Windows 開發工具鏈對齊；但指標變大也可能增加少量記憶體，因此「x64 一定更快」不是合理承諾，必須量測。[Qt for Windows](https://doc.qt.io/qt-6/windows.html)

對本專案更實際的收益是：依賴套件、CI runner、除錯工具與使用者 Windows 環境都能採主流架構，避免為已不在 Qt 官方矩陣內的 x86 自建框架。

### 3. 高 DPI 與多螢幕行為更一致

Qt 6 的高 DPI 永遠啟用，Windows 預設為 Per-Monitor DPI Aware V2；Qt Widgets 與 Qt Quick 會自動使用 device-independent 座標。Qt 6 預設 scale rounding policy 是 `PassThrough`，可精確反映 Windows 125%、150%、175% 等比例。[High DPI](https://doc.qt.io/qt-6/highdpi.html)；[Porting to Qt 6](https://doc.qt.io/qt-6/portingguide.html)

AirPodsDesktop 已設定 PerMonitorV2 manifest、`AA_EnableHighDpiScaling` 與 `PassThrough`，所以這部分主要是移除冗餘設定並得到更一致的 Qt 6 基線，而不是全新的功能。需要特別測試：

- 主視窗從不同 DPI 的螢幕顯示、隱藏與移動。
- 固定尺寸、圓角、電池自繪與字型縮放在 100/125/150/175/200%。
- 工作列位置與多螢幕「islands of screens」座標。Qt 官方明確提醒，不應假設一個螢幕外緊鄰的位置一定屬於另一個螢幕，應以 `QGuiApplication::screens()`／`QScreen` 判斷。[High DPI](https://doc.qt.io/qt-6/highdpi.html)

### 4. Multimedia 後端與 API 可維護性

Qt 6 Multimedia 做過大幅重構：裝置列舉改為 `QMediaDevices`，播放 source 改為 URL，音訊輸出改成明確的 `QAudioOutput`，playlist 從核心 API 移除。[Changes to Qt Multimedia](https://doc.qt.io/qt-6/qtmultimedia-changes-qt6.html)；[QMediaDevices](https://doc.qt.io/qt-6/qmediadevices.html)；[QMediaPlayer](https://doc.qt.io/qt-6/qmediaplayer.html)

Qt 6.11 在 Windows 預設使用 FFmpeg media backend；Qt Online Installer 的套件會帶經測試的 FFmpeg，`windeployqt`／Qt deployment tools 會部署其必要動態函式庫。FFmpeg 後端可依硬體與驅動選用 DXVA2、D3D11VA、D3D12VA 等解碼路徑，GPU texture conversion 在可用時能降低 CPU 使用；但 Qt 官方同時要求在每個目標平台測試 codec 與硬體差異。[Qt Multimedia](https://doc.qt.io/qt-6/qtmultimedia-index.html)；[Advanced FFmpeg Configuration](https://doc.qt.io/qt-6/advanced-ffmpeg-configuration.html)

對 AirPodsDesktop 的含義：AVI 動畫可能受益於較一致的 FFmpeg 解碼與硬體路徑，但也可能增加首次載入延遲、安裝大小或特定 GPU/codec 問題。這是「可量測的潛在效益」，不是遷移保證。Windows Media Foundation 原生 backend 在 Qt 6.10 起已 deprecated，Qt 官方表示會在下一個 major 移除；不應把切回 `windows` backend 當長期方案。[Qt Multimedia](https://doc.qt.io/qt-6/qtmultimedia-index.html)

### 5. Core 容器與現代 API

Qt 6 合併 QList/QVector 實作、改良 QHash 的記憶體開銷與查找設計，並把容器 size 型別改為 `qsizetype`。這些改變提供更一致的連續儲存與 64 位元容量語意，但也改變 reference invalidation 與大型物件的記憶體特性。[Changes to Qt Core](https://doc.qt.io/qt-6/qtcore-changes-qt6.html)

本專案 Qt 容器使用量很小（語系清單、設定 repository），預期效能收益有限；真正價值是移除舊 API、降低未來維護成本。必須修正任何 `int` 與 `size()` 的 narrowing warning，並避免假設 QHash/QList 元素位址穩定。

## 效能與使用流暢度：合理預期

Qt 官方移植指南明確指出，**Qt Widgets 應用程式的圖形 backend 與 Qt 5 相同**。[Porting to Qt 6](https://doc.qt.io/qt-6/portingguide.html) 因此：

- 目前 QPainter、QWidget、`.ui` 與 style sheet 不會因升級 Qt 6 自動變成 GPU UI。
- Qt Quick 在 Windows 的 Direct3D/RHI 優勢不應套用到本專案，因為 AirPodsDesktop 目前不是 Qt Quick 應用；不建議為了「可能更快」而在同一階段重寫 QML。
- Qt 6.8 官方曾改善 Windows 預設 DirectWrite font database，降低應用啟動時間，但 AirPodsDesktop 的實際改善仍需在相同硬體、冷啟動與暖啟動條件下量測。[Qt 6.8 Released](https://www.qt.io/blog/qt-6.8-released)

本專案最可能影響流暢度的因素，優先順序應是：

1. WinRT 裝置列舉／thread join 是否阻塞 GUI event loop。
2. AVI 解碼、QVideoWidget 初始化與重播方式。
3. 每次狀態更新造成的 SVG rasterization、tray icon repaint 與視窗 repaint 次數。
4. 動畫顯示／隱藏與跨 DPI 螢幕幾何計算。
5. 啟動時同步建立 Multimedia、tray、taskbar 與 scanner 的成本。

換句話說，Qt 6 應搭配效能基線與針對性調整；只換 framework 版本不構成「改善流暢度」的驗收證據。

建議在 Qt 5 與 Qt 6 用相同 Windows 10/11 x64 環境量測：

- process start → tray icon ready、scanner ready、首次主視窗可見的時間。
- 主視窗顯示後第一幀動畫時間、穩態 CPU/GPU、working set。
- 連續 100 次 advertisement 更新時 GUI thread 最大延遲與 repaint 次數。
- 低音訊延遲功能開／關的 CPU、音訊裝置切換恢復時間與耗電代理指標。
- 100/125/150/175/200% DPI 與雙螢幕切換的動畫 frame pacing。

## 相容性與必要修改

### CMake

Qt 官方說明 Qt 5.15 與 Qt 6 的 CMake API 大致相容，Qt 5.15 已提供 versionless target/command，目的就是支援漸進遷移；但同一個 executable/library 不能混用兩個 Qt major。[Qt 5 and Qt 6 compatibility](https://doc.qt.io/qt-6/cmake-qt5-and-qt6-compatibility.html)

建議先採雙版本結構：

```cmake
set(APD_QT_COMPONENTS Core Gui Widgets Svg Multimedia MultimediaWidgets LinguistTools)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS ${APD_QT_COMPONENTS})
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS ${APD_QT_COMPONENTS})

target_link_libraries(apd_gui PUBLIC
    Qt::Widgets
    Qt::Svg
    Qt::MultimediaWidgets
)
```

實作時還要：

- 在抓取 SingleApplication 前設定 `QT_DEFAULT_MAJOR_VERSION=${QT_VERSION_MAJOR}`。目前 pin 的 v3.3.0 本地 CMake 已明確支援 5/6 選擇；仍需在 Qt 6 x64 實際編譯與單例 IPC 測試。
- 測試 target 由 `Qt5::Test` 改 `Qt::Test`。
- 既有 `CMAKE_AUTOMOC/AUTORCC/AUTOUIC` 可繼續使用，不必為遷移強制改成所有 `qt_add_*` 命令。
- 不再由 `${Qt5_DIR}/../../../bin/windeployqt` 猜路徑。Qt 6.3+ 可用 `qt_generate_deploy_app_script()` 配合 `cmake --install`／CPack；它會收集 Windows runtime、plugins 與 translations。[qt_generate_deploy_app_script](https://doc.qt.io/qt-6/qt-generate-deploy-app-script.html)；[Deployment with CMake](https://doc.qt.io/qt-6/cmake-deployment.html)
- `windeployqt` 不會自動處理所有非 Qt 第三方 DLL，仍須驗證 cpr/curl、FFmpeg、MSVC runtime 與其他 vcpkg runtime 是否完整。[Qt for Windows - Deployment](https://doc.qt.io/qt-6/windows-deployment.html)

### Source/API

Qt 官方建議先在 Qt 5.15 定義 `QT_DISABLE_DEPRECATED_UP_TO=0x050F00`，把 5.15 已 deprecated 的 API 變成編譯錯誤，再升 Qt 6；也可用 Qt 6 porting Clazy checks 輔助修正。[Porting to Qt 6](https://doc.qt.io/qt-6/portingguide.html)；[Porting with Clazy](https://doc.qt.io/qt-6/porting-to-qt6-using-clazy.html)

本專案優先修改：

- 移除未使用的 `QDesktopWidget` include；所有 screen geometry 走 `QScreen`。Qt 6 已移除 `QDesktopWidget` 與 `QApplication::desktop()`。[Changes to Qt Widgets](https://doc.qt.io/qt-6/widgets-changes-qt6.html)
- `QMouseEvent::globalPos()` 改 `globalPosition().toPoint()`。
- 檢查 Qt 6 style sheet 的 enum property selector 語意；Qt 6 selector 使用 enum 名稱而非 Qt 5 的整數值。本專案目前未發現此類 selector，但新增樣式時要遵守新規則。[Changes to Qt Widgets](https://doc.qt.io/qt-6/widgets-changes-qt6.html)
- 檢查 QList/QVector `.size()` 與 `int` 的轉型 warning、QHash/QList reference lifetime。[Changes to Qt Core](https://doc.qt.io/qt-6/qtcore-changes-qt6.html)
- 移除 Qt 6 build 的 `Qt::AA_EnableHighDpiScaling`；它在 Qt 6 已無必要。保留 `setHighDpiScaleFactorRoundingPolicy(PassThrough)`，並以 DPI 測試決定是否改 `Round`。[Porting to Qt 6](https://doc.qt.io/qt-6/portingguide.html)

### Windows、Win32/MSVC 與發布架構

這是最大的非 API 成本。現況中下列位置都把 32 位元視為產品契約：

- 開發文件的 `msvc2019` 32-bit 與 `-A Win32`。
- GitHub Actions 的 `-A Win32`、Qt 5.15.2 32-bit dependency path。
- NSIS／CPack 產出中的 `win32` 目錄與檔名。
- updater 測試預期 `AirPodsDesktop-<version>-win32.exe`。
- signtool 搜尋 x86 tool path（x86 signtool 通常仍可簽 x64 binary，但應改成不依賴架構目錄的尋找方式）。

Qt 6.11 官方只列 `x86_64 + MSVC 2022`，因此建議先用現有 Qt 5.15.2 `msvc2019_64` 做一次純 x64 port，逐項驗證：

- HWND/HANDLE、`winId()`、WinRT address、process ID 與 pointer/integer cast。
- WindowsApp.lib、Boost stacktrace_windbg、vcpkg triplet 全部改為 x64 且一致。
- C++/WinRT apartment 初始化與 Qt Multimedia COM 初始化順序。Qt 官方建議先在 main thread 建立 QGuiApplication，再從其他 thread 呼叫 Multimedia API；這與本專案目前 QApplication-first 的生命週期方向相容。[Qt Multimedia on Windows](https://doc.qt.io/qt-6/qtmultimedia-windows.html)
- x86 舊客戶端如何發現與下載 x64 installer；不能只改檔名而讓既有 updater 找不到 release。
- 是否保留一個「最後版 Qt 5 x86」並顯示手動升級通知，或讓 release metadata 同時提供 x86 legacy 與 x64 Qt 6 資產。

### Qt Multimedia 詳細替代

Qt 6 Multimedia 雖稱可有限成本移植，但本專案正好用了它移除的 API：`QMediaPlaylist`、`QMediaContent` 與 Qt 5 player state/error 介面。[Changes to Qt Multimedia](https://doc.qt.io/qt-6/qtmultimedia-changes-qt6.html)

建議設計一個窄介面（例如 `MediaLoopPlayer`）隔離 GUI／低延遲功能與 Qt major 差異：

**主視窗 AVI 動畫**

- `setMedia(QUrl)` → `setSource(QUrl)`。
- `stateChanged(State)` → `playbackStateChanged(PlaybackState)`；更簡單的做法是 `setLoops(QMediaPlayer::Infinite)`，移除手動在 StoppedState 再呼叫 `play()`。[QMediaPlayer](https://doc.qt.io/qt-6/qmediaplayer.html)
- 影片本來就是 muted；Qt 6 可不為它設定 audio output，直接保留 `QVideoWidget` output。
- `QVideoWidget`／`MultimediaWidgets` 在 Qt 6 仍存在，不需要改 QML。[Qt Multimedia Widgets](https://doc.qt.io/qt-6/qtmultimediawidgets-index.html)

**低音訊延遲靜音循環**

- `QAudioDeviceInfo::availableDevices(QAudio::AudioOutput)` → `QMediaDevices::audioOutputs()`。
- 移除 `QMediaPlaylist`，player source 直接設為 `Silence.mp3` 並設 Infinite loops。
- 建立 `QAudioOutput`，連到 `QMediaPlayer::setAudioOutput()`；Qt 6 player 預設不自動連音訊裝置。[Changes to Qt Multimedia](https://doc.qt.io/qt-6/qtmultimedia-changes-qt6.html)
- error signal 改用 Qt 6 `errorOccurred(Error, QString)`，保留現有 retry 行為。
- 測試拔除／停用／切換預設 audio output，確認 `QMediaDevices` 通知後能重建 player/output；QAudioDevice snapshot 不會隨實體裝置變動而自行更新。[QAudioDevice](https://doc.qt.io/qt-6/qaudiodevice.html)

**部署與 codec**

- 對每個嵌入 AVI 與 Silence.mp3 做自動 smoke test，確認 `LoadedMedia`、第一幀、loop 與停止清理。
- 用乾淨 Windows VM 驗證 FFmpeg DLL 與 plugin 被部署；缺少 FFmpeg runtime 時會退回 native backend，而 native backend 功能較少。[Building Qt Multimedia](https://doc.qt.io/qt-6/qtmultimedia-building-from-source.html)
- 記錄 installer/portable zip 增量，並更新第三方授權與 SBOM。

### Qt Widgets

Qt 6 保留 Qt Widgets，且圖形 backend 與 Qt 5 相同，所以現有 `.ui`、QPainter、QSystemTrayIcon、QPropertyAnimation、QDialog 與自繪 Battery 可原位遷移。[Porting to Qt 6](https://doc.qt.io/qt-6/portingguide.html)

主要風險不是大重寫，而是行為回歸：

- `QDesktopWidget` 移除，改用 `QScreen`。[Changes to Qt Widgets](https://doc.qt.io/qt-6/widgets-changes-qt6.html)
- fractional DPI 造成 1px 邊界、圓角、文字截斷或固定尺寸 artifact。
- style、字型 metric、系統匣 icon rasterization 在不同 Windows 版本／DPI 的視覺差異。

因此，Qt 6 不應與 QML 重寫綁成同一專案；先保留 Widgets 可大幅縮小遷移範圍。

### SVG

Qt 6 把 Widget-dependent SVG classes 拆到 `QtSvgWidgets`；`QSvgRenderer` 仍屬 `QtSvg`。[Changes to Qt SVG](https://doc.qt.io/qt-6/qtsvg-changes-qt6.html)

AirPodsDesktop 目前只用 `QSvgRenderer` rasterize tray icon，故保留 `Qt::Svg` 即可，不需新增 `SvgWidgets`。仍應在各 DPI 比對 icon 邊緣、battery overlay 與 theme 背景。

### 翻譯

Qt 6.2+ 的 `qt_add_translations()` 由 `LinguistTools` component 提供，會建立更新 `.ts` 與產生 `.qm` 的 targets，並可指定既有 `TS_FILES`、輸出變數與 options。[qt_add_translations](https://doc.qt.io/qt-6/qtlinguist-cmake-qt-add-translations.html)

建議：

- `find_package(Qt6 REQUIRED COMPONENTS LinguistTools)`。
- 先以明列 `TS_FILES` 的方式保留 `apd_de_DE.ts` 等既有檔案與名稱，避免自動命名改變。
- 保留 `-no-obsolete`、`-locations none` 的現有內容政策。
- 明確指定或安裝 `.qm` 到目前 runtime 使用的 `translations/`；不要在同一 commit 同時把翻譯嵌入 qrc，以免增加行為變數。
- 將「更新 source catalog」與正常 build 分開，避免一般編譯改寫使用者維護中的 `.ts`。

### Qt Bluetooth

本專案目前**沒有使用 Qt Bluetooth**；AirPods advertisement、RSSI、manufacturer data 與 paired device 查詢由 C++/WinRT 實作。因此 Qt 5 → 6 不需要移植 Qt Bluetooth API，也不建議在同一次遷移順便替換成熟的 WinRT backend。

若未來考慮改用 Qt Bluetooth，需注意：

- Qt 6.2 恢復 Qt Bluetooth 時移除了舊 Win32 backend，且因此不支援 MinGW-w64 的 Qt Bluetooth；專案選 MSVC 2022 可避開 MinGW 限制。[What's New in Qt 6.2](https://doc.qt.io/qt-6/whatsnew62.html)
- Qt 官方 Bluetooth overview 對 Windows 特別列出限制：Win32 backend 無法提供 advertisement RSSI／Manufacturer Specific Data，且 discovery 只能找到先前在 Windows Settings 配對的裝置。[Qt Bluetooth Overview](https://doc.qt.io/qt-6/qtbluetooth-overview.html)
- AirPodsDesktop 的核心判斷正依賴 RSSI 與 Apple manufacturer advertisement；在沒有 PoC 證明 Qt 6 Windows backend 完整滿足這些需求前，現有 C++/WinRT 實作更符合專案需求。

可以將 WinRT Bluetooth 包在更窄的 platform service interface 以改善架構與測試，但那是專案內部重構，不是 Qt 6 必要工作。

### Qt Windows Extras

本專案目前也沒有連結 Qt WinExtras，而是直接使用 Win32/DWM/C++/WinRT，故沒有 WinExtras 編譯阻礙。

Qt 6 已移除 platform-specific Extras modules。部分 QtWin 能力移到其他 Qt API／QWindow；QWinJumpList、QWinTaskbarButton、QWinThumbnailToolBar 等則沒有完整跨平台替代，必要時需使用平台 API。[Changes to Qt Extras Modules](https://doc.qt.io/qt-6/extras-changes-qt6.html)

對 AirPodsDesktop 的建議是保留現有 Windows service layer，並維持「domain 不 include Windows API、GUI/OS adapter 才碰 HWND/DWM」的依賴方向。不要為了 Qt 6 將原本穩定的 taskbar／rounded-corner Win32 code 改寫成不存在的 WinExtras 替代。

## 第三方依賴評估

### 必須驗證

- **SingleApplication v3.3.0**：本地 fetched source 的 CMake 支援 `QT_DEFAULT_MAJOR_VERSION` 5/6，但預設是 5。Qt 6 build 必須在 FetchContent 前設定為 6，並測試 x64 的 shared memory、local socket、secondary instance activation。
- **spdlog 1.8.5、cxxopts 2.2.1、cpr 1.7.2、nlohmann-json 3.9.1、magic_enum 0.7.3**：全部是多年以前的 pin。它們不直接依賴 Qt，但需以 MSVC 2022 x64 重新編譯；Qt 6 遷移不應在同一 commit 一次升所有版本，以免無法定位 ABI／行為回歸。
- **vcpkg baseline/triplet**：目前 manifest baseline 與 CI bootstrap 流程需改 `x64-windows`，並鎖定可重現 baseline；避免 CI 每次 clone 最新 vcpkg 卻依賴舊 manifest constraint。
- **Boost stacktrace_windbg**：確認 x64 PDB alt path、DbgEng/runtime 與 release package 行為。
- **FFmpeg runtime**：Qt 6 Multimedia 帶來新的動態依賴與授權資料；部署工具通常會複製必要 library，但最終 installer 必須在乾淨 VM 驗證。[Qt Multimedia](https://doc.qt.io/qt-6/qtmultimedia-index.html)

### 不要混用 Qt major

Qt 官方明確指出同一 library/executable 不支援混用 Qt 5 與 Qt 6。[Qt 5 and Qt 6 compatibility](https://doc.qt.io/qt-6/cmake-qt5-and-qt6-compatibility.html) 所有 transitively linking Qt 的依賴（特別是 SingleApplication）必須與 app 使用同一個 major、compiler 與 architecture。

## 成本與風險

以下為依目前程式碼規模的**本專案推估**，不是 Qt 官方工期承諾；假設一位熟悉 C++/Qt/Windows 的工程師、已有 Windows 10/11 測試環境，且不包含 QML 重寫。

| 工作包 | 推估 | 主要風險 |
|---|---:|---|
| Qt 5 x64 基線、WinAPI/WinRT audit | 2–4 工程日 | updater/installer 資產契約、指標轉型 |
| 雙版本 CMake、SingleApplication、LinguistTools | 2–4 工程日 | FetchContent Qt major、翻譯輸出、部署腳本 |
| Qt 6 source compile fixes | 2–4 工程日 | deprecated API、容器型別、signal signature |
| Multimedia port | 3–6 工程日 | AVI/MP3、loop、裝置熱插拔、FFmpeg backend |
| CI、NSIS、portable、簽章、updater x64 | 3–5 工程日 | 舊 x86 客戶升級、release naming |
| GUI/DPI/Bluetooth/audio/manual regression | 5–10 工程日 | 多螢幕、裝置差異、Windows 10/11 |
| 文件、授權、SBOM、發布演練 | 2–4 工程日 | FFmpeg/第三方 notices |
| **合計** | **19–37 工程日** | 約 4–8 週單人，另加 canary/field soak |

最大風險排序：

1. **x86 → x64 發布與自動更新過渡**：失敗會讓既有用戶無法升級。
2. **Multimedia 行為差異**：是唯一明顯需要重寫的 Qt 功能區。
3. **Windows Bluetooth／audio 裝置實機矩陣**：CI 很難完全覆蓋。
4. **DPI/工作列客製 GUI**：編譯成功不代表視覺正確。
5. **第三方 pin 與部署 DLL**：開發機可跑但乾淨機缺 runtime 的風險。

## 建議分階段策略

### Phase 0：建立基線與決策門檻

- 確認產品是否接受 Windows 10 1809+／Windows 11 x64；若必須支援 32 位元 Windows，停止 Qt 6 正式遷移決策。
- 在現行 Qt 5 build 記錄啟動、首次顯示、動畫 CPU/GPU、記憶體、advertisement 壓力與 DPI 截圖基線。
- 固定一組真實裝置矩陣：至少 AirPods Pro/Pro 2、Windows 10、Windows 11、單／雙螢幕、不同 audio output 狀態。

### Phase 1：先做 Qt 5 x64

- 改用 Qt 5.15.2 `msvc2019_64` 與 vcpkg `x64-windows`，不動 Qt API。
- 修正 CI、NSIS、portable、簽章與 updater asset naming。
- 同時發布或內部測試 x86/x64，驗證設定、單例、Bluetooth、media control、installer upgrade。

**Go/No-Go：** x64 功能與 Qt 5 Win32 等價，舊版 updater 能可靠導向 x64 installer。

### Phase 2：清理 Qt 5.15 deprecated API

- 加入 `QT_DISABLE_DEPRECATED_UP_TO=0x050F00` 的專用 migration CI job。
- 移除 QDesktopWidget、整理 QScreen/high-DPI、建立 event position 相容 helper。
- 把 Multimedia 操作封裝成窄介面，先不改 backend。

**Go/No-Go：** Qt 5 x64 全部測試與手動流程保持通過。

### Phase 3：CMake 雙版本與 Qt 6 編譯

- 採 versionless targets；CI 建立 Qt 5 x64 與 Qt 6.11.2 x64 matrix。
- SingleApplication 明確跟隨 Qt major。
- 翻譯改到 Qt 6 LinguistTools API；部署改用 Qt CMake deployment API。
- 一個 commit 只處理一類可驗證變更，避免 Qt 6、第三方全面升級、QML 重寫混在一起。

### Phase 4：Multimedia port 與乾淨機部署

- 完成 QMediaDevices/QAudioOutput/QMediaPlayer source/loops/error 遷移。
- 針對每個 AVI、MP3、audio device 熱插拔做測試。
- 乾淨 Windows VM 驗證 installer 與 portable，不依賴開發機 PATH、codec pack 或 Qt 安裝。

**Go/No-Go：** 所有 media 資產可載入、首幀與 loop 正確、安裝包不缺 DLL、低音訊延遲功能在裝置切換後可恢復。

### Phase 5：效能與 UX 驗收

- 以 Phase 0 同一方法比較 Qt 5 x64 與 Qt 6 x64。
- 任何「更快」主張需有量測；若退化，先辨識是 FFmpeg plugin、資源格式、font database、repaint 或 thread blocking。
- 完成 100–200% DPI、多螢幕、Windows 10/11 視覺對照。

### Phase 6：canary 與切換

- 先提供 prerelease/canary，收集 Bluetooth adapter、audio driver、GPU/codec 差異。
- 保留最後一版 Qt 5 x86 可下載，但不讓兩個架構共用含糊的 asset 名稱。
- 正式版切 Qt 6 後，至少一個 release cycle 保留可回退的 Qt 5 x64 build pipeline；不要讓正式 app 同時載入 Qt 5/6。

## 驗收清單

### Build/CI

- [ ] MSVC 2022 x64 RelWithDebInfo configure/build/test 全通過。
- [ ] Qt 6.11.2 版本被 CI 明確鎖定，不依賴 runner 偶然安裝。
- [ ] 所有 target 與 SingleApplication 使用同一 Qt major/architecture。
- [ ] `windeployqt` 或 Qt deployment script 的輸出可重現。
- [ ] installer 與 portable 在乾淨 Windows 10/11 VM 啟動。

### 功能

- [ ] BLE advertisement、RSSI、manufacturer data、配對裝置查詢。
- [ ] 開盒顯示、關盒隱藏、綁定/解除、耳朵偵測、全域媒體控制。
- [ ] tray/taskbar icon、右鍵 menu、tooltip、通知。
- [ ] 每個 AirPods/Beats AVI 首幀、loop、切換、停止與釋放。
- [ ] Silence.mp3 低延遲模式與 audio output 插拔／停用／切換。
- [ ] 更新檢查、下載、SHA-256、x86 舊版到 x64 新版升級。
- [ ] 所有 `.qm` 可被 `QTranslator` 從 `translations/` 載入。

### UX/效能

- [ ] 100/125/150/175/200% DPI，單螢幕與混合 DPI 雙螢幕。
- [ ] 視窗出入動畫無跳動、截斷、1px gap 或錯誤 screen geometry。
- [ ] 啟動、首幀、穩態 CPU/GPU、working set 不劣於事先定義門檻。
- [ ] 壓力 advertisement 更新時 GUI event loop 無可感卡頓。

## 最終決策

**建議核准 Qt 6 遷移，但以「Qt 6.11.2 + MSVC 2022 + x64 + 保留 Widgets」為目標，並先完成 Qt 5 x64 階段。**

升級的確定收益是支援週期、安全維護、現代 Windows 工具鏈、高 DPI 基線與可持續的 Multimedia API；效能收益主要可能來自 x64 生態、Qt 6.8+ Windows 啟動改善與 FFmpeg/hardware decode，但對這個 Widgets 專案都必須量測，不能預先保證。

若組織已購買 Qt 商業授權且優先需求是五年維護，而不是最新 minor，可將生產基線改為 Qt 6.8 LTS；否則開源專案應採最新受支援 Qt 6 minor（評估日為 6.11.2），並預算每 6–12 個月更新 minor。Qt minor 通常每年兩次，非 LTS minor 一般只在下一 minor 前提供兩三個 patch，官方商業標準支援是一年。[Qt Releases](https://doc.qt.io/qt-6/qt-releases.html)

最後，Qt 6.11 文件已註明 Qt 6.12 將是最後支援 Windows 10 的版本；產品應在遷移同時訂出 Windows 10 終止政策，而不是把此問題留到 Qt 6.13 才處理。[Qt for Windows](https://doc.qt.io/qt-6/windows.html)
