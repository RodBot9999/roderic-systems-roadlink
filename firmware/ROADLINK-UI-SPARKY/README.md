# ROADLINK-UI-SPARKY

This is a separate, presentation-oriented RoadLink firmware sketch. The
original `firmware/RoadLink` sketch is untouched. Open
`ROADLINK-UI-SPARKY.ino` in Arduino IDE to build this version.

The uploaded 320 x 240 Roderic Systems splash image and its press-to-continue
caption are unchanged. The new code-drawn interface uses a six-card home
dashboard, colored streaming tiles, static paged detail cards, clearer alert
selection, a redesigned boot check, and an OTA indicator. All original rotary
actions, manual detail-page switching, explicit Back choices, startup checks,
CAN, GPS, OBD-II, A7670SA, streaming, settings, and reboot behavior remain.

The small `OTA` indicator is green only when the local firmware update access
point started. The Settings > About page shows its address. Use the full
[local update instructions](LOCAL_FIRMWARE_UPDATE.md) for the Wi-Fi password,
first USB upload, and later browser uploads.

Settings also has three SPARKY-only persistent controls:

- **Heartbeat:** ON by default. OFF suppresses future LTE `/heartbeat` requests
  and pauses periodic configuration check-ins, but normal telemetry START and
  STOP still work. Telemetry responses can still carry configuration. A
  request already in progress may finish.
- **OTA Wi-Fi:** ON by default. OFF closes the update web server and turns off
  the ESP32 access point; turn it back ON with the rotary knob whenever needed.
  It does not erase the saved Wi-Fi password or affect the A7670SA.
- **Color Theme:** five small code-drawn palettes. The selection is saved to
  NVS and applied immediately, without changing the original splash image.

Long detail values appear on full-width, stationary cards. Rotate the knob to
move between pages when needed; pages never advance automatically or scroll.

For the first installation, use USB and the **Default 4MB with spiffs** OTA
partition scheme on a matching 4 MB ESP32 board. Do not erase all flash if you
want to retain NVS settings. Keep the vehicle stationary and power stable for
any firmware update. The UI was compile-verified, but physical TFT appearance
and rotary behavior still need a board test before presentation.
