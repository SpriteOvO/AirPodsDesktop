# AirPodsDesktop 架構與效能升級藍圖

> 評估日期：2026-09-01
> 範圍：目前 working tree 的靜態盤點、既有 Qt Test 基線、Windows/Qt 5 執行路徑
> 關聯文件：[Qt 6 遷移評估](Qt6MigrationAssessment.md)

## 決策摘要

建議採「**先建立效能基線與低風險熱點修正，再抽出兩條高價值 seam，最後遷移 Qt 6**」的順序：

1. 先降低目前版本的 idle work 與 GUI thread 風險。
2. 將 AirPods session 與 media playback 做成深 module，讓 Windows/Qt 細節成為 adapter。
3. 先完成 Qt 5 x64，再以 Qt 5/Qt 6 雙建置遷移到 Qt 6 x64。
4. 保留 Qt Widgets；目前沒有證據支持在同一階段重寫 QML。

Qt 6 的確定價值是維護週期、x64/MSVC 2022、DPI 與 Multimedia 現代化；目前使用流暢度的最大改善機會則在專案自己的排程、鎖、輪詢與重繪路徑。

## 現況診斷

### 1. CMake 分層存在，但 source seam 尚未真正成立

目前 target 名稱表達了 `apd_support → apd_domain → apd_core`，以及 `apd_presentation → apd_gui`；然而實際 header 依賴使 domain 直接知道 Qt 與 Windows：

- `Source/Core/AirPods.h` 同時 include `QObject`、`QString`、`Bluetooth.h`。
- `Bluetooth.h` 在 Windows 直接 include `Bluetooth_win.h`，而後者暴露 C++/WinRT 型別。
- `Source/Core/Base.h` 的 domain value object 直接使用 Logger 與 `QString` formatter。
- GUI header 直接 include `Core/AirPods.h`、`Core/Update.h`、`Core/Settings.h`，因此 GUI 編譯面等同依賴整個 core。

結果是：Qt major、WinRT 與 domain logic 無法獨立編譯；測試也必須連結 `apd_core` 與 Qt。這個 module 的 interface 太寬，Qt 6 compile error 會穿透到本來不該受影響的 protocol/state tests。

### 2. 高頻 callback 在持鎖狀態執行

`Helper::Callback::Invoke()` 在 `_mutex` 鎖住時逐一呼叫 subscriber。Bluetooth watcher callback 隨後再取得 `AirPods::Manager::_mutex`，並在同一鎖定區間內完成 advertisement parse、state merge、signal emission，甚至在入耳狀態改變時呼叫全域媒體控制。

這會造成三個風險：

- 慢 subscriber 直接延長 BLE callback latency。
- callback 若註冊／解除同一 callback collection，容易形成 re-entrancy 或 deadlock。
- `GlobalMedia_win.cpp` 的 fallback media key 路徑含 50 ms sleep；若從 state transition 直接呼叫，就把外部副作用放進 state lock。

同類問題也出現在 Settings：`SaveWithoutLock()` 與 `ApplyChangedFieldsOnlyWithoutLock()` 名稱中的「WithoutLock」其實代表 caller 已持有 manager mutex；repository I/O 與八種 settings side effect 都可能在鎖內執行。`Application.cpp` 已針對 device lookup join 加 queued workaround，說明這不是純理論風險。

### 3. 工作列狀態在 idle 時每 100 ms 輪詢 Win32

`TaskbarStatus` 啟用後以 100 ms timer 呼叫 `GetTaskBarInfo()`。每次會重新 `FindWindowW`／`FindWindowExW`，並執行多次 `GetWindowRect` 與 `MapWindowPoints`。也就是功能啟用後，即使工作列沒有任何變化，仍約每秒做十次完整查詢。

這是目前最明確、最容易驗證的 idle CPU／wake-up 熱點。建議用 Windows event/message 驅動工作列／顯示器變更，並保留低頻 fallback timer，而不是單純把 100 ms 改成另一個魔法數字。

### 4. 狀態更新造成重複 presentation 與 tray rasterization

每次 `StateUpdated` 會分送到 MainWindow、TrayIcon、TaskbarStatus。三者各自保存狀態並立即 repaint。TrayIcon 每次 repaint 都重新建立 64×64 image、重新解析／render SVG、重算文字與 overlay，再建立 QPixmap/QIcon。

StateManager 已過濾部分 duplicate advertisement，但 UI 仍缺少 presentation equality 與 dirty-field 判斷。相同的 battery/status presentation 不應重建 SVG 或重設所有 widget property。

### 5. Multimedia 與大型動畫資產增加啟動／發布成本

- 十個 AVI 加一個 MP3 直接嵌入 qrc；目前 video source 合計約 16.8 MB，既有 executable 約 22.4 MB。
- MainWindow 建構時立即建立 `QMediaPlayer`／`QVideoWidget` 並啟動 update checker，即使使用者只讓程式常駐 tray。
- LowAudioLatency controller 在 application prepare 時立即建立並嘗試初始化 player，即使功能關閉。
- Qt 5 以 stopped signal 手動重新 `play()` 模擬 loop；Qt 6 可用 player 原生 infinite loops。

這些不是「AVI 一定慢」的證明，但足以要求量測 cold start、first frame、CPU/GPU、working set 與 installer delta，再決定保留 video、改 codec/container、改 sprite animation，或把資產移到獨立檔案。

### 6. 測試基線可用，但沒有涵蓋最危險的 seams

目前 17 個 Qt Test case 涵蓋 advertisement parsing/state merge、settings repository、update metadata/hash 與 MainWindow presentation。測試目標可成功編譯，補上 Qt `bin` PATH 後 1/1 CTest 通過。

缺口包括：

- scanner lifecycle、callback re-entrancy、lock ordering 與停止中的 callback。
- bound-device lookup 取消／過期結果／錯誤結果。
- GUI event-loop latency、tray icon cache、taskbar relocation。
- media first frame、loop、audio output hotplug 與部署完整性。
- Qt 5 x86 → Qt 5 x64 → Qt 6 x64 的 installer/updater 過渡。

測試執行本身還依賴手動把 `Qt5Test.dll` 所在目錄加進 PATH；一般 `ctest` 會因缺少 DLL 以 `0xc0000135` 結束。這應在 build/test infrastructure 修正，讓本機與 CI 使用相同的可重現測試入口。

## 目標架構

目標不是增加大量薄 wrapper，而是建立少數深 module：caller 只學一個小 interface，複雜度留在 implementation。只有 production adapter 與 test adapter 都確實需要時才建立 port。

```text
apd_bootstrap
  └─ 組裝 application、Windows adapters、Qt views

apd_gui (Qt Widgets)
  └─ render immutable presentation snapshots
       ↓
apd_presentation
  └─ AppPresentation / MainWindowPresentation / TrayPresentation
       ↓
apd_application
  ├─ AirPodsSession
  ├─ SettingsController
  └─ UpdateController
       ↓ ports
apd_platform_windows
  ├─ WinRtAdvertisementSource
  ├─ WinRtDeviceCatalog
  ├─ WinGlobalMediaController
  ├─ WinTaskbarHost
  └─ QSettingsRepository
       ↓
apd_domain (pure C++20)
  ├─ Apple Continuity packet parser
  ├─ AirPodsStateReducer
  └─ value objects / transitions
```

### AirPodsSession：第一條高價值 seam

建議 interface 僅表達 caller 真正需要的行為：

```cpp
class AirPodsSession {
public:
    void Start(SessionConfig config);
    void Stop();
    void SetBoundDevice(std::optional<DeviceId> id);
    SessionSnapshot Snapshot() const;
    // 單一狀態 stream；Qt adapter 可轉成 signal。
};
```

implementation 內部擁有 scanner、device catalog、state reducer、clock 與 media-control ports。GUI 不再取得 `Bluetooth::Device`，也不需要知道 WinRT enumeration 或 callback thread。

測試從這條 interface 驗證 observable outcomes：送入 advertisement、connection event 與時間，斷言 session snapshot／domain event。production 使用 WinRT adapters，測試使用 in-memory adapters；這是兩個真實 adapter，因此 seam 有存在價值。

### SettingsController：縮小 side-effect interface

目前 `ApplyObserver` 有八個方法，而且每加一個 setting 就擴大 interface。建議改成：

```cpp
SettingsSnapshot Load();
SettingsChangeSet Update(const SettingsPatch &patch);
SettingsSnapshot Snapshot() const;
```

在短鎖內只更新 immutable snapshot 並計算 change set；解鎖後再持久化及由 application coordinator 執行 side effects。QSettingsRepository 與 MemoryRepository 保留為同一 seam 的兩個 adapters。

這會消除「accessor destructor 隱式存檔與套用」的 ordering constraint，並讓失敗模式可以顯式回傳，而不是藏在 RAII destructor 中。

### Presentation snapshots：避免三套 UI 狀態機漂移

保留現有 presentation module 並深化：由 application snapshot 一次產生 `MainWindowPresentation`、`TrayPresentation`、`TaskbarPresentation`。每個 view 只比較新舊 presentation 並更新 dirty fields。

離散事件（開盒、關盒、解除綁定）立即送達；高頻 telemetry（battery/RSSI）可只保留 latest value，最多以 display refresh 所需頻率送 GUI。不要把 lid event 與 battery coalescing 混成同一條延遲規則。

## 效能優先級

### P0：先量測，1–2 工程日

在 Qt 5 x86 現況與後續 Qt 5 x64／Qt 6 x64使用同一套 marker：

- process start → tray ready、scanner ready、MainWindow first visible、video first frame。
- advertisement received → reducer complete → GUI render complete。
- GUI event loop 超過 16/50/100 ms 的 stall 計數。
- idle 5 分鐘 CPU、wake-up、working set；taskbar feature on/off 分開。
- 100 次 synthetic advertisement 的 callback duration、repaint count、icon generation count。
- DPI 100/125/150/175/200%，單螢幕與 mixed-DPI 雙螢幕。

先以相對門檻驗收：任何 phase 不得讓 p95 latency、idle CPU 或 working set 退化超過 10%；有基線後再設定絕對 SLO。

### P1：低風險、可獨立交付，5–8 工程日

1. `TaskbarStatus` 改事件驅動；fallback polling 降到不高於 1 Hz，steady state 目標為零輪詢。
2. TrayIcon cache base SVG raster 與最終 icon，cache key 至少包含 battery text、update dot、DPI/theme；presentation 未變就不 repaint。
3. `Helper::Callback::Invoke()` 在鎖內複製 subscriber snapshot，解鎖後呼叫。
4. Settings 在鎖內只更新 state；repository I/O 與 side effect 全部移到鎖外。
5. MainWindow media 改 lazy initialization；LowAudioLatency 只在功能啟用時建立 player/output。
6. 修正 CTest runtime PATH／deployment，使一條命令可重現執行。

每項分開 commit、分開量測。不要先新增通用 event bus 或 thread pool；目前沒有足夠變體支持那些 seams。

### P2：抽出 domain/session seams，8–15 工程日

1. 先將 packet parser、state reducer 與 value objects 移成 Qt/WinRT-free domain。
2. 建立 AirPodsSession，將 WinRT callback marshal、取消、stale-result filtering 與 media side effect 收進 implementation。
3. 新測試只穿過 AirPodsSession interface；成熟後刪除測穿內部 shallow module 的重複測試。
4. SettingsController 改 explicit update/result，移除 accessor destructor side effect。

完成標準：`apd_domain` 可在不連結 Qt／Windows runtime 的情況下編譯與測試；GUI header 不再 include Windows Bluetooth 型別。

### P3：Qt 5 x64 與 Qt 6，19–37 工程日

依 [Qt 6 遷移評估](Qt6MigrationAssessment.md) 執行：

1. Qt 5.15.2 x64 與 updater/installer 過渡。
2. Qt 5 deprecated API 清理與 Qt major-neutral CMake。
3. Qt 6.11.2 x64 source、Multimedia、deployment 與 DPI regression。
4. Canary 後切換正式版；保留最後 Qt 5 x64 pipeline 一個 release cycle。

P1/P2 的 media seam 與 pure domain 會縮小 Qt 6 變更面，但不要等「完美重構」才開始 x64 build spike。架構與平台移植應以小步交錯，而不是建立長期分支。

## 驗收指標

| 面向 | 建議門檻 |
|---|---|
| Idle | Taskbar steady state 不做 100 ms polling；reference machine median CPU 目標 < 0.2% |
| 互動 | tray click → window 可互動 p95 < 100 ms（不含首次 video decode） |
| BLE | advertisement → presentation commit p95 < 100 ms；不允許 GUI task > 50 ms |
| 動畫 | warm first frame < 200 ms；顯示／隱藏無肉眼跳動；frame pacing 以 reference trace 驗收 |
| 穩定性 | scanner stop/destruction 後零 callback；settings/media side effect 不在 state mutex 內 |
| 記憶體 | Qt 6 x64 steady working set 相對 Qt 5 x64 不退化 > 10%，否則須有原因與核准 |
| 發布 | clean Windows VM 不靠 Qt PATH/codec pack；x86 舊版可可靠取得 x64 installer |
| 測試 | 單一 documented command 可 build + ctest；domain tests 不需 GUI/multimedia runtime |

絕對數字需以 Phase P0 的 reference hardware 校準；這些值是初始產品門檻，不是未量測前的效能承諾。

## 建議 commit／PR 切分

1. `test: add performance markers and reproducible test runtime`
2. `fix: replace taskbar polling with change notifications`
3. `perf: cache tray icon presentation renders`
4. `refactor: invoke callbacks outside registry locks`
5. `refactor: apply settings side effects outside state locks`
6. `perf: initialize multimedia only when requested`
7. `refactor: isolate AirPods domain state reduction`
8. `refactor: introduce the AirPods session seam`
9. `chore: establish Qt 5 x64 release pipeline`
10. 依 Qt 6 文件分拆 CMake、source API、Multimedia、deployment 與 docs commits。

每一個 commit 都應保持可建置、可測試；Qt 6、第三方全面升級、QML 重寫與 UI redesign 不得混成同一個 PR。

## 第二次驗證紀錄

2026-09-01 重新執行獨立驗證，結果如下：

- `apd_gui` 與 `AirPodsDomainTests` 以 Win32 `RelWithDebInfo` 重新建置成功。
- `AirPodsDesktop.vcxproj` 的 `ClCompile` 與 `Link` targets 成功，0 warning、0 error；既有 `Application.obj` 時間戳晚於目前 `Application.cpp`，確認當前 source 已納入編譯輸出。
- 在該次 test process 將 `C:\Qt\5.15.2\msvc2019\bin` 加入 PATH 後，CTest 再次 1/1 通過，耗時 0.08 秒；未提供該 runtime path 時缺少 `Qt5Test.dll` 的 infrastructure 問題仍成立。
- 建置前後逐一比較所有 `.ts` 的 SHA-256，結果完全相同；驗證過程沒有觸發 `APD_CREATE_UPDATE_TS` 或改寫使用者翻譯檔。
- source 搜尋再次確認 100 ms taskbar polling、鎖內 subscriber invocation、tray SVG 每次 rasterization、eager Multimedia initialization 與 domain include WinRT/Qt 等證據仍存在。
- Qt 官方版本表再次確認 Qt 6.11.2 為目前 Qt 6.11 patch、標準支援至 2027-03-17；Windows 支援組合為 Windows 10 1809+/Windows 11 的 x86_64 + MSVC 2022 或 MinGW-w64，不包含 x86。官方亦再次確認 Qt Widgets graphical backend 與 Qt 5 相同，以及 `QMediaPlaylist`／`QMediaContent` 已從 Qt 6 Multimedia 移除。

第二次驗證沒有發現足以修改優先順序、目標架構或 Qt 6 決策的反證；原評估結論維持不變。

## 最終建議

核准架構與 Qt 6 升級，但把成功定義成可量測的產品結果：更少 idle work、沒有鎖內外部副作用、GUI thread 無長任務、domain 可獨立測試、Qt 6 x64 可在乾淨 Windows 環境穩定部署。

第一個實作 milestone 應是 **P0 + P1**，而不是直接改 `find_package(Qt6)`。完成後再進入 AirPodsSession seam 與 Qt 5 x64；這樣即使 Qt 6 時程調整，現有使用者仍會先得到流暢度與穩定性改善。
