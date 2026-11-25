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

`AirPods4AncThreeSources.txt` contains a second 30-second capture from 2026-10-19,
with left battery alternating between 10%, 90%, and unavailable:

- Source 1 reported left unavailable and right 90%.
- Source 2 reported both pods at 90%.
- Source 3 reported left 10% and right unavailable.

Sources 1 and 2 both advertised as the right side. The partial report from source 1
must not replace the selected complete report from source 2 and open a path for
source 3's conflicting reading. The replay checks that both batteries remain at
90% after the complete source is first selected. Source identifiers are local to
each capture and do not establish physical device identity.
