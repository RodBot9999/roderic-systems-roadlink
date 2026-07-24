# SIM800L Telemetry

RoadLink sends current GPS and OBD-II snapshots as authenticated JSON through a
SIM800L GPRS connection. `Sim800Service` is a non-blocking state machine, so
modem delays do not stop CAN, GPS, OBD, web, or TFT updates.

## Wiring

| ESP32 | SIM800L |
|---|---|
| GPIO16 TX | RX |
| GPIO17 RX | TX |
| GPIO2 | RST |
| GND | GND |

The firmware uses the module's confirmed `115200` baud default. It also enables
the ESP32 RX pull-up so a disconnected modem lead remains at the UART idle-high
level instead of producing noise interrupts.

Use a supply designed for the modem's transmit-current bursts. Do not power a
SIM800L from an ESP32 GPIO or assume the board's 3.3 V regulator is sufficient.

## One-time carrier configuration

Set the installed SIM card's carrier values in `firmware/RoadLink/AppConfig.h`:

```cpp
constexpr char SIM_APN[] = "YOUR_CARRIER_APN";
constexpr char SIM_APN_USER[] = "";
constexpr char SIM_APN_PASSWORD[] = "";
```

There is no compiled receiver or tunnel URL.

## Cellular-to-desktop test

1. Start `RoadLinkMonitor.exe`.
2. Wait for `Public endpoint ready`.
3. On the RoadLink main menu open `SIM / Cellular`.
4. Enter the displayed public IPv4 address, TCP port, and six-digit key.
5. Select `Send telemetry now`, or enable automatic sending.

Rotate the knob to change the selected IP octet or digit. Press to advance.
Pressing the final value saves it. Back cancels. These settings are stored in
ESP32 NVS and survive restart.

The desktop app requests a temporary router mapping through NAT-PMP or UPnP,
renews it while running, and removes it on close. This requires compatible
router configuration and a real public IPv4 address; it cannot bypass CGNAT.

## Device page

The SIM page includes status/diagnostics, modem and auto-send toggles, receiver
IP, receiver port, access key, immediate send, interval selection, and
reset/reconnect. `Data to send` opens checkbox-style selectors for GPS and
OBD-II telemetry. Either group can be enabled independently; the selection is
saved in NVS. The access key is formatted as exactly six digits, including
leading zeros.

At startup, CAN is initialized before the optional modem UART. An enabled modem
is then given a fixed time to answer `AT`. No response creates a warning in
Startup Diagnostics but never leaves the firmware waiting indefinitely. Press
the encoder during this probe to skip it immediately. If the modem is disabled
in settings, its boot check displays `DISABLED` without a warning.

## Payload

```json
{
  "access_key": "123456",
  "device": "roadlink",
  "uptime_ms": 123456,
  "gps": {
    "valid": true,
    "latitude": 34.123456,
    "longitude": -118.123456,
    "speed_kmh": 42.5
  },
  "obd": {
    "rpm": 1850,
    "speed_kmh": 43,
    "coolant_c": 91
  }
}
```

Unavailable readings are JSON `null`. The receiver removes the key before
displaying or logging an accepted packet.

## AT-command flow

The service verifies SIM readiness and registration, attaches GPRS, opens bearer
profile 1, configures the runtime `http://IP:PORT/telemetry` URL, uploads JSON,
issues `AT+HTTPACTION=1`, records the HTTP status, and terminates HTTP. Failed
steps retry without blocking the rest of RoadLink.
