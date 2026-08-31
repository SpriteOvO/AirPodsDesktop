# Performance Notes

AirPodsDesktop is intended to remain active in the notification area, so background CPU usage is
treated as a product constraint rather than only a benchmark metric. Pull request
[#199](https://github.com/SpriteOvO/AirPodsDesktop/pull/199) supplied the original measurements and
implementation work; pull request [#210](https://github.com/SpriteOvO/AirPodsDesktop/pull/210)
integrated and extended that work for version 0.5.1.

## Recorded idle baseline

The author of #199 measured process CPU time over a 60-second idle window on Windows. These values
are retained as a historical regression baseline:

| Build | Process CPU time | Approximate total CPU usage |
| --- | ---: | ---: |
| Installed 0.4.1 | 8.73 seconds | 0.91% |
| Rebuilt #199 branch | 0.42 seconds | 0.04% |

The measurements came from one machine and were not collected by an automated benchmark. Hardware,
logical processor count, Bluetooth state, audio devices, taskbar configuration, and enabled settings
can all affect the result. Use the numbers to detect large regressions, not as a universal performance
guarantee.

## What reduced background work

The 0.5.1 implementation incorporates the main lifecycle changes explored in #199:

- BLE scanning follows the bound device's connection state and ignores unrelated manufacturer data
  before parsing it.
- Hidden tray-popup animations stop decoding frames.
- Low-latency audio uses a generated silent PCM stream and only keeps the output active while the
  bound device is connected.
- Tray icons are reused until their rendered battery state changes.
- Taskbar geometry polling slows down after its initial positioning pass.

## Reproducing an idle measurement

1. Build the Win32 `RelWithDebInfo` configuration described in [Build.md](Build.md).
2. Start AirPodsDesktop with the same device binding, Bluetooth state, taskbar settings, and
   low-latency audio setting for every candidate build.
3. Allow startup work and device discovery to settle before recording a sample.
4. Record the process CPU-time increase across a fixed 60-second window. Repeat the sample several
   times and report the median together with the machine's logical processor count.
5. Note whether the AirPods were connected and whether the main window or tray popup was visible.

When converting process CPU time to average total CPU usage, use:

```text
total CPU usage (%) = process CPU seconds / (elapsed seconds * logical processors) * 100
```

Keep the raw process CPU-time value in reports so results can still be compared across machines with
different processor counts.
