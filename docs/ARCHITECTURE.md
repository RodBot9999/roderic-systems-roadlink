# Roderic Systems RoadLink Architecture

ESP32 automotive CAN/GPS/OBD scanner with a renderer-independent UI, a 2.4-inch SPI TFT, a local phone web app, and live WebSocket updates.

## Required Arduino libraries

- **ESP32 by Espressif Systems** board package.
- **mcp_can** by Cory J. Fowler for the MCP2515.
- **WebSockets** by Markus Sattler / Links2004 for `WebSocketsServer.h`.
- **Adafruit GFX Library**.
- **Adafruit ILI9341**.
- `WiFi.h`, `WebServer.h`, and `SPI.h` are included with the ESP32 board package.

## Confirmed hardware mapping

| Function | ESP32 GPIO |
|---|---:|
| Encoder CLK | 25 |
| Encoder DT | 26 |
| Encoder button | 27 |
| GPS PPS | 34 |
| GPS TX -> ESP32 RX | 35 |
| GPS RX <- ESP32 TX | 32 |
| MCP2515 CS | 23 |
| MCP2515 SO / shared MISO | 19 |
| MCP2515 SI / shared MOSI | 18 |
| MCP2515 / TFT shared SCK | 5 |
| MCP2515 INT | 4 |
| TFT CS | 12 |
| TFT DC | 14 |
| TFT RESET | 15 |

The TFT and MCP2515 share SCK, MOSI, and MISO. Each device has a separate CS pin, so only the selected SPI peripheral drives the bus.

GPIO12 and GPIO14 are the confirmed working TFT control pins on the assembled unit. Earlier GPIO22/GPIO21 assignments caused unresolved hardware behavior and are no longer used.

The TFT is configured as a 320x240 landscape ILI9341 display. Its LED/backlight pin is treated as external hardware and is not driven by an ESP32 GPIO in this firmware.

## Startup sequence

1. The TFT initializes on the shared custom SPI bus.
2. The uploaded 320x240 `RODERIC SYSTEMS / CAN DIAGNOSTICS` splash is drawn from a compact 2-bit image stored in flash and remains visible until the encoder is pressed.
3. After the press, a live system-check screen reports:
   - rotary input;
   - UI core;
   - GPS receiver UART;
   - OBD service;
   - MCP2515 CAN controller;
   - local WebSocket server.
4. CAN initialization failures are registered in the existing startup diagnostics system.
5. When the checks finish, the diagnostics screen remains visible for three seconds. Pressing the encoder skips this delay.
6. The normal tree menu opens. If CAN failed, the first rendered frame is the module-error menu with the existing override option.

`GPS RECEIVER OK` means the ESP32 UART/PPS service initialized. Satellite fix quality remains a separate live GPS status.

## Permanent navigation rule

The interface remains a folder/tree controlled by one rotary encoder:

- Rotate left/right: move the selection.
- Press: enter or activate.
- Every submenu and detail screen contains an explicit `< Back` option.
- The phone also provides a direct Back button, but the explicit Back item remains part of the shared UI model.

## Rendering architecture

`MenuSystem` builds one canonical `UiFrame`. All renderers consume the same frame and revision:

```text
CAN / OBD / GPS services
          |
          v
      MenuSystem
          |
          v
       UiModel
       /  |   \
      /   |    \
 TFT renderer  WebSocket renderer  Serial mirror
```

### `TftRenderer`

- uses Adafruit ILI9341 + Adafruit GFX;
- renders in 320x240 landscape;
- uses a black/cyan industrial-cyber visual language;
- renders complete menu lists with a selection bar and scrollbar;
- renders data as compact two-column field cells;
- automatically pages detail screens containing more than ten fields;
- shows the currently selected detail action in the footer;
- redraws only when the `UiModel` revision or TFT detail page changes;
- converts unsupported UTF-8 symbols such as degree marks into ASCII-friendly labels.

### Splash storage

`SplashImage.h` stores the uploaded four-color PNG as a packed 2-bit-per-pixel image:

- original image: 320 x 240;
- packed pixel data: 19,200 bytes;
- palette: four RGB565 colors;
- no PNG decoder or SD card is required at boot.

## Main menu tree

```text
Scan-Track-Log
├── CAN Tools
│   ├── CAN Status
│   ├── Passive Monitor
│   │   ├── Live Frame
│   │   ├── ID Browser
│   │   └── Back
│   ├── OBD-II Scanner
│   │   ├── OBD Status
│   │   ├── Discover ECUs
│   │   ├── Live Data
│   │   ├── Supported PIDs
│   │   ├── Read DTCs
│   │   ├── Clear DTCs
│   │   ├── Read VIN
│   │   └── Back
│   ├── Bus Statistics
│   ├── Clear CAN Statistics
│   ├── Retry CAN Controller
│   └── Back
├── GPS Tools
│   ├── Overview
│   ├── Position
│   ├── Fix / Signal
│   ├── Motion
│   ├── Time / Date
│   ├── NMEA Statistics
│   ├── Raw NMEA
│   ├── Reset GPS Statistics
│   └── Back
└── Settings
    ├── CAN Bitrate
    ├── CAN Operating Mode
    ├── Serial CAN Stream
    ├── Serial UI Mirror
    ├── Web UI
    ├── UI Refresh
    ├── OBD Poll Interval
    ├── About
    └── Back
```

## Web interface

The ESP32 creates its own local access point:

```text
SSID: RodBot-Scanner
Password: rodtracklog
Address: http://192.168.4.1
HTTP port: 80
WebSocket port: 81
```

The phone UI remains completely local. Its style was updated to match the TFT's black/cyan industrial-cyber theme. The WebSocket and TFT still consume the exact same `UiFrame`; neither contains duplicate navigation logic.

## CAN and OBD-II architecture

`CanService` owns the MCP2515 receive/transmit paths, frame statistics, identifier browser, diagnostic response queue, and guarded bidirectional transmission.

`ObdService` is a non-blocking standard 11-bit ISO 15765-4 OBD-II state machine supporting ECU discovery, live PIDs, supported-PID masks, stored DTCs, DTC clearing confirmation, VIN, and ISO-TP multi-frame assembly.

The controller still boots in Listen-only mode. Active OBD functions switch it to Normal mode because standardized requests require transmission.

## File responsibilities

### `RoadLink.ino`
Composition root, TFT boot sequence, system checks, service initialization, and non-blocking update loop.

### `AppConfig.h`
Physical pins, TFT rotation/frequency, network configuration, queue sizes, and timing defaults.

### `TftRenderer.*`
ILI9341 renderer, splash drawing, startup checks, menu/data/alert layouts, field pagination, and revision-based updates.

### `SplashImage.h`
Packed 2-bit version of the uploaded 320x240 splash image.

### `UiModel.*`
Canonical renderer-independent screen state and optional Serial mirror.

### `WebUiService.*`
Local HTTP page, live WebSocket transport, JSON serialization, and phone input queue.

### `MenuSystem.*`
Tree navigation, screen definitions, actions, settings, and conversion of service snapshots into `UiFrame` objects.

### `CanService.*`, `ObdService.*`, `GpsService.*`, `EncoderInput.*`
Hardware and protocol services, independent of the active renderer.

## Recommended first TFT test

1. Install Adafruit GFX and Adafruit ILI9341 in Library Manager.
2. Confirm TFT VCC/GND/backlight wiring before powering.
3. Compile and upload.
4. Verify the splash fills the display in landscape orientation and waits for an encoder press.
5. Confirm the startup-check screen updates CAN and WebSocket status, then opens the first page after three seconds or immediately when pressed.
6. Rotate and press the encoder through every menu.
7. Verify the phone and TFT always show the same logical screen.
8. If the TFT appears rotated or inverted, change `AppConfig::TFT_ROTATION` between `1` and `3`.

## Validation status

The new `TftRenderer.cpp` passed a host-side C++17 syntax check using compatibility stubs. The full embedded build still requires Arduino IDE with the exact ESP32, Adafruit, MCP_CAN, and WebSockets library versions, followed by testing on the physical ILI9341 clone and shared SPI bus.
