# RoadLink desktop configuration TODO

The ESP32 no longer exposes a phone page or accepts screen-navigation commands.
The future desktop app must exchange semantic configuration through the existing
LTE receiver.

## Device heartbeat

While the modem and receiver endpoint are enabled, the firmware sends
`POST /heartbeat` every 30 seconds, independently of telemetry START/STOP.
The JSON contains the access key, device ID, uptime, and the complete applied
streaming configuration:

```json
{
  "access_key": "123456",
  "device": "roadlink",
  "type": "heartbeat",
  "uptime_ms": 12345,
  "config": {
    "revision": 7,
    "running": false,
    "gps": true,
    "obd": true,
    "obd_fields": 135,
    "interval_seconds": 10
  }
}
```

Telemetry payloads include the same `config` object so the app can reconcile
state even if it misses a heartbeat.

## Receiver response

An empty response or `{ "ok": true }` acknowledges the request without changing
the device. To update RoadLink, return one complete atomic configuration object:
Keep the complete response within 1,536 bytes. Larger responses are not applied
as configuration, keeping modem response memory bounded.

```json
{
  "ok": true,
  "config": {
    "revision": 8,
    "running": true,
    "gps": true,
    "obd": true,
    "obd_fields": 135,
    "interval_seconds": 10
  }
}
```

The firmware accepts only revisions newer than its applied revision. It saves
the interval, module choices, field mask, and revision in NVS. `running` remains
volatile for safe boot. The next heartbeat reports what was actually applied.
Repeated or stale revisions are ignored without triggering another immediate
heartbeat. A due heartbeat takes precedence over the next telemetry request;
an in-progress modem transaction must finish first, so 30 seconds is a target
interval rather than a guaranteed network delivery deadline.

OBD field bits are: RPM 0, speed 1, coolant 2, throttle 3, manifold pressure 4,
intake temperature 5, ignition timing 6, ECU voltage 7, and fuel level 8.

## Desktop app work intentionally not implemented here

- Add `/heartbeat` handling to the receiver.
- Keep the latest applied configuration per device.
- Queue a newer desired revision and return it until the device reports it.
- Show pending, applied, rejected, and offline states in the dashboard.
- Use dashboard controls and icons; never recreate the TFT screen or send
  rotary/menu-navigation commands.
- Add stronger authenticated transport before allowing sensitive commands.

Protocol reference: [SIMCom A76XX AT Command Manual V1.09, section 16.2.6](https://download.kamami.pl/p1189472-A76XX_Series_AT_Command_Manual_V1.09.pdf).
HTTP response reads use an explicit offset/length and wait for the body and
completion trailer after the initial acknowledgment.
