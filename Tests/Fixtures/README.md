# Battery broadcast regression capture

`AirPods4AncConflictingBattery.txt` contains 48 observations collected over 30 seconds on
2026-10-19 while the left battery display alternated between 40% and 90%.

- Both sources reported model `0x201B` (AirPods 4 with ANC).
- Source 1 reported left 40%, right unavailable, with generally weaker RSSI.
- Source 2 reported left/right 90%/90% with generally stronger RSSI.
- The user checked the phone: left 94%, right 89%, consistent with source 2's coarse readings.

Source numbers replace Bluetooth addresses. Only timing, decoded battery buckets, state flags,
and RSSI are retained; no Bluetooth addresses or encrypted advertisement payloads are included.
Battery values 0–10 are ten-percent buckets; 15 denotes unavailable data in this capture.

The replay test preserves packet order and values but does not wait between observations. It
checks source conflict handling, not the existing ten-second advertisement expiry timers.
The source-selection filter remains a heuristic: unauthenticated broadcast data alone does not
prove that two advertisements belong to the same paired headphones.
