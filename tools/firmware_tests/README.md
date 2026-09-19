# Firmware host regression tests

These tests compile the actual `A7670Service.cpp`, state definitions, parser,
configuration, and GPS/OBD snapshot structs with host-only Arduino/UART/time
substitutes. No modem, upload, or real HTTP connection is used. They are not a
replacement for the hardware checklist in
[A7670SA Telemetry](../../docs/A7670SA_TELEMETRY.md).

On Windows with a C++17 GCC/Clang-compatible compiler:

```powershell
.\tools\firmware_tests\run.ps1 -Compiler 'C:\path\to\g++.exe'
```

Or a portable Zig compiler:

```powershell
.\tools\firmware_tests\run.ps1 -Compiler 'C:\path\to\zig.exe' -Zig
```

The runner generates a translation unit and executable in a uniquely named
temporary folder, printed at completion. It does not modify the firmware or
Arduino core/libraries.

Coverage: AT query versus unsolicited/fragmented replies; heartbeat-only boot and
refresh; warm enable; endpoint validation; START/STOP; STOP during HTTPDATA;
pause during pending AT; searching/denial and bounded operator recovery;
radio/attach recovery; partial HTTP result; UART reset with checks disabled;
and a modem that does not acknowledge reset. The modem suite also checks
heartbeat fairness during fast telemetry, reading remote configuration, and
valid JSON with missing GPS fixes and selected OBD fields.

A second suite compiles the production GPS, OBD and StreamingController code
with fake CAN, persistence and modem boundaries. It checks GPS fix acquisition,
staleness, loss and recovery; automatic active CAN polling for selected PIDs;
resuming after manual diagnostics; module toggles; interval limits; persistence
calls; remote configuration revisions and retrying a deferred remote START.

Hardware follow-up: upload the sketch, configure the receiver, then exercise
Streaming > Sources and the HH:MM:SS editor. Short press selects/toggles; hold
the encoder for 850 ms to return. Confirm saved choices after reboot, GPS
recovery outdoors, and selected OBD values without opening Live Data. Confirm
STOP stops telemetry while the 30-second heartbeat continues. The receiver
must implement `/heartbeat` before that endpoint can succeed; app work is
tracked in `firmware/RoadLink/REMOTE_CONFIG_TODO.md`.
