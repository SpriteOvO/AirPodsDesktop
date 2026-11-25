# Connect automatically when audio starts playing

In Settings, select the paired Bluetooth audio device and enable **Connect automatically when
audio starts playing**. The option is off by default and is independent of the tray-click option.
Both options use the same selected device.

When an output audio session starts running, the application checks the selected device and requests
a connection only if it is disconnected. A device that is already connected causes no notification
or window to open. Successful automatic connections open the battery window; progress and failures
use the existing application notification card.

Playback is checked on a background worker every 250 ms across active Windows output endpoints.
The application's own sounds are excluded. Audio session state is used rather than sound level,
so muted playback and quiet passages still count as playback. No audio is recorded. At least 1.5
seconds without a running output session is required before another playback start is recognized.
Applications that keep their audio streams running while paused may therefore delay recognition of
a subsequent playback start.

Each playback start can request one connection. Existing connection requests are reused. After an
automatic failure or timeout, at least 30 seconds must pass **and a new playback start must occur**
before another automatic attempt. The application does not continuously retry during playback.
Turning the option off stops playback monitoring; a connection request already submitted to Windows
is allowed to finish.

## Validation

- Build x64 `RelWithDebInfo` and run `ctest --test-dir Build -C RelWithDebInfo --output-on-failure`.
- Tests cover the independent setting, persistence/observer application, playback edges, duplicate
  requests, already-connected behavior, cooldown, monitor shutdown and manual clicks during an
  automatic check. UI tests use in-memory settings and do not write user preferences.
- An optional native Windows audio smoke test plays a quiet two-second tone in a separate process
  and checks that the real playback probe detects its start and stop. Stop other playback first:

  ```powershell
  $env:APD_RUN_AUDIO_SMOKE_TEST = '1'
  .\Build\Binary\QuickConnectTests.exe nativePlaybackProbeDetectsExternalPlayback
  Remove-Item Env:APD_RUN_AUDIO_SMOKE_TEST
  ```

For a hardware check, enable the option, disconnect the selected headphones, and start playback
on the PC. Confirm that one connection attempt occurs and the battery window opens on success.
Repeat while already connected and with the option off; neither case should initiate another
connection. With the headphones unavailable, confirm that continuous playback does not keep
retrying, and that restarting playback after the cooldown allows another attempt.
