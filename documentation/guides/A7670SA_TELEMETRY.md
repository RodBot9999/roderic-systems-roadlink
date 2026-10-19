# A7670SA Telemetry

RoadLink sends current GPS and OBD-II snapshots as authenticated JSON through a
A7670SA LTE Cat-1 connection. `A7670Service` is a non-blocking state machine, so
modem delays do not stop CAN, GPS, OBD, web, or TFT updates.

## Wiring

| ESP32 | A7670SA |
|---|---|
| GPIO16 TX | RX |
| GPIO17 RX | TX |
| GND | GND |

The firmware uses the module's confirmed `115200` baud default. It also enables
the ESP32 RX pull-up so a disconnected modem lead remains at the UART idle-high
level instead of producing noise interrupts.

Only power, ground, TX, and RX are connected. Reset, sleep/DTR, and PWRKEY are
not connected to the ESP32. `Disable modem checks` pauses firmware use only;
the physical modem remains powered and may stay registered. `Reconnect modem`
rechecks existing status rather than power-cycling the module.

Use the input voltage specified by the exact A7670SA breakout board and a supply
designed for its transmit-current bursts. Do not power it from an ESP32 GPIO or
assume the board's 3.3 V regulator is sufficient.

## One-time carrier configuration

If the SIM/modem has no working automatic APN, set the installed carrier values
in `firmware/RoadLink/AppConfig.h`:

```cpp
constexpr char SIM_APN[] = "YOUR_CARRIER_APN";
constexpr char SIM_APN_USER[] = "";
constexpr char SIM_APN_PASSWORD[] = "";
```

There is no compiled receiver or tunnel URL.
The placeholder APN is never written to the module. An existing active LTE
default bearer/APN is reused; an explicitly configured APN is applied only to
an inactive context. Changing an already-active carrier APN may require a modem
reset/power cycle.

## Cellular-to-desktop test

1. Start `RoadLinkMonitor.exe`.
2. Wait for `Public endpoint ready`.
3. On the RoadLink main menu open `A7670SA / Cellular`.
4. Enter the displayed public IPv4 address, TCP port, and six-digit key.
5. Wait for modem checks to finish, then select the green `START` option.
6. Select `STOP` to prevent further HTTP posts.

Boot, re-enable, reconnect, and reboot always leave telemetry stopped, even if
an older firmware saved auto-send ON. START sends the first packet and enables
interval sending; `Send telemetry now` is available only while running.
Opening an endpoint/key editor stops telemetry too. STOP cannot recall an HTTP
request already sent, but it prevents the next HTTPACTION. Pending AT commands
and local HTTPDATA input are allowed to finish safely before cleanup.

Rotate the knob to select an individual digit, then press to enter digit-change
mode. Rotation now cycles only that digit through `0`-`9`; press again to return
to position selection. You can move in either direction to correct any digit.
Rotating beyond the digits selects `SAVE CHANGES` or `CANCEL / BACK`. IP octets
must be `000`-`255`, and ports must be `00001`-`65535`; invalid values remain in
the editor with an explanation. Saved settings are stored in ESP32 NVS and
survive restart.

The desktop app requests a temporary router mapping through NAT-PMP or UPnP,
renews it while running, and removes it on close. This requires compatible
router configuration and a real public IPv4 address; it cannot bypass CGNAT.

## Device page

The cellular page includes status/diagnostics, persistent failure details,
modem-check enable/disable, green START/red STOP, receiver IP, receiver port, access key, immediate
send, interval selection, and software reconnect. `Data to send` opens
checkbox-style selectors for GPS and OBD-II telemetry. Either group can be
enabled independently; the selection is saved in NVS. The access key is
formatted as exactly six digits, including leading zeros.

Status and error screens never change pages automatically. Rotate the encoder
to select a page and press to return. `Errors / failure details` retains the
latest failure after recovery and shows the failed state, failure type, AT
command, captured modem response, retry target, age, count, HTTP status, and a
targeted troubleshooting hint.

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

## Reboot and warm-enable checks

The main-menu `Reboot` action requires confirmation. It stops telemetry/OBD
activity, settles any pending modem command, requests `AT+CRESET`, and restarts
the ESP32 to repeat the splash and all startup checks. NVS connection settings
are kept; runtime counters and error history restart from zero. GPS and other
external devices remain physically powered: there is no reset/power wiring.
If the modem is absent or reset is not acknowledged, ESP32 restart still
proceeds. Usually this takes seconds; an outstanding operator selection or
HTTP service command may take minutes. The maximum fallback is 315 seconds.

Normal LTE searching is displayed as waiting, not repeated AT-command errors.
Denied registration is retained in the Errors page. After a 120-second
registration window, automatic operator selection is attempted once per
outage; status polling continues afterward without repeatedly forcing it.

## AT-command flow

The service drains stale UART replies, synchronizes AT, enables verbose modem
errors, and disables ambiguous registration URCs. It checks `AT+CPIN?`,
`AT+CFUN?`, `AT+COPS?`, `AT+CEREG?`, `AT+CSQ`, `AT+CGATT?`,
`AT+CGACT?`, `AT+CGDCONT?`, and `AT+CGPADDR=1`. Radio/attach commands
are issued only when their status requires them. No `AT+CGACT=1,1` is forced on
the LTE default bearer. Status is refreshed about every 30 seconds.

Only after START, each post runs `AT+HTTPINIT` (which can establish PDP data), configures the
runtime `http://IP:PORT/telemetry` URL and JSON content type, uploads the body
with `AT+HTTPDATA`, issues `AT+HTTPACTION=1`, records the HTTP status, and calls
`AT+HTTPTERM`. Failed steps retry without blocking the rest of RoadLink.

Command formats and response deadlines follow the manufacturer
[A76XX AT-command manual](https://files.waveshare.com/wiki/A7670E-Cat-1-GNSS-HAT/A76XX_Series_AT_Command_Manual_V1.09.pdf).

## Hardware regression checklist

1. Power on with a saved, valid receiver endpoint: allow checks for at least
   30 seconds and verify the receiver gets no HTTP packets before START.
2. Disable modem checks without removing modem power, then enable again:
   verify registration/status recovers and telemetry remains STOPPED.
3. START: verify packets arrive; STOP: wait beyond the interval and confirm
   no new requests (an already-submitted request may finish).
4. Disable/STOP during HTTPDATA or operator selection: verify checks recover
   without leaving the UART in payload-input mode.
5. Reboot: confirm ESP32 startup repeats, modem reset is requested, endpoint
   settings remain, and packets do not resume until START.
6. Repeat reboot with modem disconnected: ESP32 must still restart.
