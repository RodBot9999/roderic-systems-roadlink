# Getting Started

## Required software

- Arduino IDE 2.x
- ESP32 board package by Espressif Systems
- `mcp_can` by Cory J. Fowler
- `WebSockets` by Markus Sattler / Links2004
- Adafruit GFX Library
- Adafruit ILI9341

`WiFi.h`, `WebServer.h`, and `SPI.h` are supplied by the ESP32 board package.

## Confirmed GPIO mapping

| Function | ESP32 GPIO |
|---|---:|
| Encoder CLK | 25 |
| Encoder DT | 26 |
| Encoder button | 27 |
| GPS PPS | 34 |
| SIM800L RST | 2 |
| ESP32 TX to SIM800L RX | 16 |
| ESP32 RX from SIM800L TX | 17 |
| GPS TX -> ESP32 RX | 35 |
| GPS RX <- ESP32 TX | 32 |
| MCP2515 CS | 23 |
| Shared SPI MISO | 19 |
| Shared SPI MOSI | 18 |
| Shared SPI SCK | 5 |
| MCP2515 INT | 4 |
| TFT CS | 12 |
| TFT DC | 14 |
| TFT RESET | 15 |

GPIO12 and GPIO14 are the confirmed working TFT control pins on the assembled unit. Earlier GPIO22/GPIO21 experiments caused unresolved hardware behavior and are not used by the current firmware.

The TFT and MCP2515 share SCK, MOSI, and MISO. Each device has its own chip-select pin.

The GPIO table follows the assembled RoadLink carrier PCB. It is not a generic
ESP32 wiring suggestion: the TFT control pins and custom MCP2515 SPI mapping
were selected from the connections verified on the physical prototype.

## Uploading

1. Clone or download the repository.
2. Open `firmware/RoadLink/RoadLink.ino`.
3. Install any missing libraries through Arduino Library Manager.
4. Select the ESP32 board that matches the installed hardware.
5. Select the correct serial port.
6. Verify/compile before uploading.
7. Upload and open Serial Monitor at `115200` baud.

Before cellular testing, configure `SIM_APN` in `AppConfig.h`. Start the
desktop monitor, then enter its displayed public IP, port, and access key under
the main-menu `SIM / Cellular` page; RoadLink saves them in NVS.

The startup diagnostics wait a bounded interval for GPS serial bytes and an
enabled SIM800L AT response. Missing modules produce overridable warnings.
`OVERRIDE AND CONTINUE` is initially selected so one press proceeds.

## Expected startup

1. A full-screen Roderic Systems splash appears.
2. Press the rotary encoder to continue.
3. The display reports rotary input, UI, GPS, OBD, CAN, and WebSocket status.
4. The completed status page advances after three seconds or immediately when the encoder is pressed.
5. If CAN initialization fails, RoadLink opens the module-error screen with an override option.

## Local web interface

The default firmware creates its own Wi-Fi access point:

```text
SSID: RodBot-Scanner
Password: rodtracklog
Address: http://192.168.4.1
```

These values can be changed in `firmware/RoadLink/AppConfig.h`.

## First hardware checks

- Confirm common ground between every module.
- Confirm the TFT power and backlight requirements for the exact display clone.
- Confirm the MCP2515 oscillator and CAN bitrate configuration.
- Test the device away from active driving.
- Begin with CAN listen-only mode.
- If the display is inverted, change `TFT_ROTATION` between `1` and `3`.

## Troubleshooting

### Backlight on, no image

Check TFT controller compatibility, CS `GPIO12`, DC `GPIO14`, RESET `GPIO15`, shared SPI wiring, and ground.

### CAN initialization error

Check MCP2515 power, CS `GPIO23`, INT `GPIO4`, SPI wiring, oscillator selection, and the configured bitrate.

### Web page unavailable

Confirm the phone is connected to the RoadLink access point and open `http://192.168.4.1` directly.

### SIM800L does not register

Check the SIM card, antenna, carrier 2G/GSM availability, UART direction, common ground, and module power supply. The SIM800L can draw large current bursts and should not be powered directly from the ESP32 3.3 V pin.

Before running the full firmware, use the standalone
[SIM800L baud scanner and AT terminal](https://github.com/RodBot9999/roderic-systems-roadlink/tree/codex/current-build-tools/tools/SIM800L_Baud_Terminal)
from the current-build branch to verify the modem's UART rate and `AT` response.
A modem should answer plain `AT` even without a SIM card. Confirm that ESP32
GPIO16 transmits to SIM800L RX and that SIM800L TX drives ESP32 GPIO17. Never
apply the modem's 4 V supply to an ESP32 GPIO.
