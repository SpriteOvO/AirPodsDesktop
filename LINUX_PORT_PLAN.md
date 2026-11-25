# AirPodsDesktop Linux Port Plan

## Status

As of 2026-11-19, the Linux port builds and runs, with the main desktop features implemented:

- [x] Linux CMake build, bundled runtime, DEB and tarball packaging.
- [x] BlueZ Bluetooth discovery, AirPods binding and battery/connection display.
- [x] MPRIS ear-detection playback control, BlueZ Quick Connect, XDG autostart, tray and settings.
- [x] System theme detection through the desktop portal, with a Qt fallback.
- [x] Linux tray noise control for supported models, using the bound classic Bluetooth device.
- [x] Linux automated suite: 14 of 14 CTest entries passed in the latest run, including noise-control,
      media, domain and UI tests. These tests do not replace hardware validation.

### Hardware observations and remaining work

- [x] On AirPods Pro 3, the user reported Noise Cancellation, Transparency, Adaptive and Off working.
      Device reports in the trace independently confirmed Noise Cancellation and Adaptive; the later
      Transparency and Off selections were not captured in that trace.
- [x] On 2026-11-16 the Quick disconnect tray action was verified on AirPods Pro 3: the device
      disconnected and the state cleared; the AirPods reconnected by themselves about 45 seconds
      later. The first test showed a spurious noise-control "AAP connection closed" warning, because
      the control socket notices the link drop before BlueZ reports it. Error notifications are now
      held briefly and dropped if the device disconnects; a re-test on 2026-11-17 produced no warning.
- [ ] Verify noise control and the rest of the port across additional AirPods models, firmware,
      Bluetooth adapters and desktop environments. Earlier Off and repeated Adaptive requests timed
      out; their causes were not established. Off may require enabling Off Listening Mode on an Apple
      device, but a timeout alone does not establish that this is the cause.
- [ ] Verify Wayland on Linux.
- [x] Publish Linux release assets. The tag workflow attaches the DEB and tarball to the draft
      release, and the in-app updater selects the DEB for package-managed installations. Local
      RelWithDebInfo DEB and tarball builds succeeded and their extracted payloads matched.
- [ ] Confirm the first tagged Linux release on a clean machine. CI installs the DEB and smoke-tests
      the packaged application, but a Release build and clean-machine installation still need
      manual acceptance testing.

GNOME requires an AppIndicator extension for the tray. The taskbar battery dock is Windows-only.
See `Docs/Build.md` for build, packaging and noise-control limitations.

## Original plan (historical)

The phases, examples and estimates below record the original proposal, not the current
implementation or remaining work. The feature parity checklist is updated separately. In particular,
the Bluetooth packet and scanning examples are
illustrative and should not be used as protocol or implementation references; consult the source and
`Docs/Build.md` instead. Current status is tracked above.

## Executive Summary
AirPodsDesktop uses Qt with platform-specific subsystems for Bluetooth, media control, auto-start, and audio management. The codebase has well-structured abstract interfaces, making it suitable for a Linux port. Estimated effort: **2-3 months** for a complete port with full feature parity, or **3-4 weeks** for a minimal viable port (Bluetooth + battery display only).

---

## Phase 1: Build System (Week 1)

### 1.1 Update CMakeLists.txt
- **Remove**: Hard Windows-only checks (lines 51-62)
- **Add**: Platform detection and conditional compilation
- **Add**: Linux-specific find_package calls for BlueZ, D-Bus, GLib, ALSA/PipeWire

```cmake
# Platform detection
if(WIN32)
    set(APD_PLATFORM "windows")
elseif(UNIX AND NOT APPLE)
    set(APD_PLATFORM "linux")
endif()

# Conditional dependencies
if(APD_PLATFORM STREQUAL "linux")
    find_package(Bluez REQUIRED)
    find_package(DBus REQUIRED)
    find_package(Glib REQUIRED)
    find_package(ALSA REQUIRED)
endif()
```

### 1.2 Platform-Specific Source Files
```cmake
# Windows sources
if(APD_PLATFORM STREQUAL "windows")
    target_sources(apd_core PRIVATE
        Source/Core/Bluetooth_win.cpp
        Source/Core/AutoStart_win.cpp
        Source/Core/GlobalMedia_win.cpp
        Source/Core/QuickConnect_win.cpp
    )
endif()

# Linux sources (new files to create)
if(APD_PLATFORM STREQUAL "linux")
    target_sources(apd_core PRIVATE
        Source/Core/Bluetooth_linux.cpp
        Source/Core/AutoStart_linux.cpp
        Source/Core/GlobalMedia_linux.cpp
        Source/Core/QuickConnect_linux.cpp
    )
endif()
```

### 1.3 Linux Dependencies
```bash
# Ubuntu 24.04
sudo apt install \
    libbluetooth-dev \
    libdbus-1-dev \
    libglib2.0-dev \
    libasound2-dev \
    libpipewire-0.3-dev \
    libpulse-dev \
    libx11-dev \
    libwayland-dev
```

---

## Phase 2: Bluetooth Subsystem (Weeks 2-3)

### 2.1 Replace WinRT Bluetooth with BlueZ D-Bus API

**Current**: `Bluetooth_win.cpp` uses WinRT `Windows.Devices.Bluetooth.Advertisement` for BLE scanning.

**Linux Implementation**: Use BlueZ's D-Bus API via `gdbus` or `sd-bus`.

```cpp
// Example: Start BLE advertising scan via D-Bus
g_dbus_connection_emit_signal(conn, NULL, "/org/bluez/hci0",
    "org.bluez.LEAdvertisingManager1", "RegisterAdvertisingApplication",
    parameters, NULL);
```

### 2.2 Parse Apple AirPods BLE Advertisements
Apple uses manufacturer data with company ID 0x004C (76 decimal). The payload format is well-documented.

**Key Limitation:** BlueZ D-Bus API does NOT support filtering by manufacturer data at the D-Bus level. You must filter in application code by inspecting each device's `ManufacturerData` property.

**Advertisement Format (Paired Mode, 27 bytes total):**
```
Byte 0-2:  0x07 0x19 0x01 (protocol header)
Byte 3-4:  Device model (e.g., 0x0220 = AirPods 1)
Byte 5:    UTP flags
Byte 6:    Case battery level (bits 0-3) + charging flag (bit 6)
Byte 7:    Left bud battery (bits 0-3) + right bud battery (bits 4-7)
Byte 8:    Lid state
Byte 9-26: Encrypted status data
```

**Battery values use 10% steps (0-10), not exact percentages. Value 0xF = unavailable.**

```cpp
// C example: Parse AirPods battery from manufacturer data
#include <stdint.h>

typedef struct {
    int model;
    int case_battery;      // -1 = unavailable
    int left_battery;      // -1 = unavailable
    int right_battery;     // -1 = unavailable
    bool case_charging;
    bool lid_open;
} AirPodsBattery;

static int percent(int nibble) {
    if (nibble > 10) return -1;  // 0xF = unavailable
    return nibble * 10;
}

bool parse_airpods_battery(const uint8_t *data, int len, AirPodsBattery *out) {
    if (len < 27 || data[0] != 0x07 || data[1] != 0x19 || data[2] != 0x01) {
        return false;  // Not AirPods format
    }
    
    out->model = data[3] | (data[4] << 8);
    out->case_battery = percent(data[6] & 0x0F);
    out->left_battery = percent(data[7] & 0x0F);
    out->right_battery = percent((data[7] >> 4) & 0x0F);
    out->case_charging = (data[6] & 0x40) != 0;
    out->lid_open = (data[6] & 0x08) != 0;
    
    return true;
}
```

### 2.3 Implement Device Manager Interface
Implement `DeviceManagerAbstract` interface:
- Device discovery via BlueZ
- Device connection state monitoring
- Battery level reporting

---

## Phase 3: System Tray and GUI (Week 4)

### 3.1 System Tray Integration
Qt's `QSystemTrayIcon` works on Linux via DBus StatusNotifierItem.

```cpp
// Check if tray is available
if (QSystemTrayIcon::isSystemTrayAvailable()) {
    trayIcon->show();
} else {
    // Fallback: keep window visible
    showWindow();
}
```

### 3.2 GNOME Desktop Integration
For GNOME, use AppIndicator/KStatusNotifierItem extension:
- Check if `org.gnome.Shell.Extensions.AppIndicator` is installed
- Provide fallback behavior if not

### 3.3 Wayland Support
Qt handles Wayland natively. Ensure proper environment variables:
- `WAYLAND_DISPLAY`
- `XDG_CURRENT_DESKTOP`

---

## Phase 4: Media Control (Weeks 5-6)

### 4.1 MPRIS D-Bus Integration
Linux uses MPRIS (Media Player Remote Interfacing Specification) for media control.

```cpp
// Query active MPRIS players
QDBusInterface player("org.mpris.MediaPlayer2.playerctl",
    "/org/mpris/MediaPlayer2",
    "org.mpris.MediaPlayer2.Player");

// Control playback
player.call("PlayPause");
```

### 4.2 Media Key Handling
- Use libmpd or libpulse for audio session detection
- Handle media key events via X11 or Wayland protocols

---

## Phase 5: Audio Latency Optimization (Week 7)

### 5.1 PipeWire/ALSA Integration
For low-latency audio, use ALSA or PipeWire directly:

```cpp
// ALSA low-latency setup
snd_pcm_t *handle;
snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
snd_pcm_set_params(handle, ...);  // Configure for low latency
```

### 5.2 Bluetooth Codec Support
- Verify A2DP codec support on Linux
- Test with aptX, LDAC, SBC codecs
- Ensure low-latency codec selection works

---

## Phase 6: Auto-Start and System Integration (Week 8)

### 6.1 XDG Autostart
Write autostart `.desktop` file:

```cpp
// Create autostart entry
QDir dir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation));
dir.mkpath("autostart");
QFile file(dir.filePath("airpods-desktop.desktop"));
file.open(QIODevice::WriteOnly);
file.write("[Desktop Entry]\nType=Application\nExec=airpods-desktop\n");
```

### 6.2 Systemd User Service
Optional: Provide systemd user service for background operation.

---

## Phase 7: Testing and Validation (Week 9+)

### 7.1 Test Matrix
- **OS**: Ubuntu 24.04 LTS (GNOME default)
- **Bluetooth**: Test with various Bluetooth adapters
- **Audio**: Test with PulseAudio, PipeWire, ALSA
- **Wayland/X11**: Test both display servers

### 7.2 Feature Parity Checklist

These checkboxes indicate implementation, not comprehensive hardware verification.

- [x] AirPods battery level display
- [x] Connection status indicator
- [x] Media playback control through MPRIS
- [x] Quick Connect through BlueZ
- [x] Quick disconnect for the selected Quick Connect device (verified on AirPods Pro 3)
- [x] Auto-start on boot
- [x] Tray icon with menu
- [x] Settings persistence
- [x] Noise control for supported models (see hardware observations above)

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| BlueZ API changes | Low | High | Pin BlueZ version, test on multiple distros |
| MPRIS compatibility | Low | Medium | Test with multiple media players |
| Wayland limitations | Medium | Medium | Provide X11 fallback |
| Bluetooth codec issues | Medium | High | Thorough codec testing |
| GNOME integration | Low | Low | Document workarounds |

---

## Dependencies Summary

| Dependency | Purpose | Linux Equivalent |
|------------|---------|------------------|
| WinRT Bluetooth | BLE scanning | BlueZ D-Bus API |
| Windows Registry | Auto-start | XDG autostart |
| Windows MMDevice | Audio control | PulseAudio/PipeWire |
| Win32 API | Window management | X11/Wayland |
| Windows Media Control | Media control | MPRIS D-Bus |
| KSPROPERTY | Bluetooth audio | BlueZ + ALSA |

---

## Success Criteria

1. **Builds on Ubuntu 24.04** without Windows dependencies
2. **Detects AirPods** via BLE scanning
3. **Displays battery level** in system tray
4. **Controls media playback** on the active player
5. **Supports both X11 and Wayland** display servers
6. **Passes comprehensive testing** with various Bluetooth adapters and audio configurations

---

## Estimated Timeline

| Phase | Duration | Cumulative |
|-------|----------|------------|
| Build System | 1 week | 1 week |
| Bluetooth Subsystem | 2 weeks | 3 weeks |
| System Tray & GUI | 1 week | 4 weeks |
| Media Control | 2 weeks | 6 weeks |
| Audio Latency | 1 week | 7 weeks |
| System Integration | 1 week | 8 weeks |
| Testing & Validation | 1+ week | 9+ weeks |

**Total: 9-12 weeks for production-ready port**
