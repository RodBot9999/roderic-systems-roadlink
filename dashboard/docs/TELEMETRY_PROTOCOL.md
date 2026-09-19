# RoadLink telemetry receiver and protocol

RoadLink Fleet 0.2.0 implements the same direct HTTP receiver as the current RoadLink repository's Windows monitor. It is useful for prototypes before a hosted fleet service exists.

## Current topology

```text
RoadLink + cellular modem
  POST http://IP:PORT/telemetry
              |
              v
Electron main process
  - HTTP listener on 0.0.0.0
  - six-digit access-key check
  - per-client authentication rate limit
  - JSONL append-only log
  - NAT-PMP, then UPnP (optional)
              |
        narrow Electron IPC
              |
              v
React renderer
  - live device state
  - map and GPS trajectory
  - telemetry charts
  - locally editable fleet metadata
```

The renderer never opens sockets, runs system commands, reads the access-key file, or writes telemetry logs. Those responsibilities stay in the trusted Electron main process. The preload exposes only typed receiver operations and events.

## Firmware-compatible request

The receiver listens on port 8080 by default. Its exact endpoint is:

```text
POST /telemetry
Content-Type: application/json
```

The current firmware payload is accepted unchanged:

```json
{
  "access_key": "123456",
  "device": "roadlink",
  "uptime_ms": 123456,
  "gps": {
    "valid": true,
    "latitude": 20.674800,
    "longitude": -103.347500,
    "altitude_m": 1566.2,
    "speed_kmh": 42.5,
    "course_deg": 82.1,
    "satellites": 11,
    "utc_time": "201531.00",
    "utc_date": "040826"
  },
  "obd": {
    "rpm": 1850,
    "speed_kmh": 43,
    "coolant_c": 91,
    "throttle_pct": 22.4,
    "map_kpa": 45,
    "intake_c": 31,
    "timing_deg": 17.5,
    "voltage_v": 14.08,
    "fuel_pct": 68.0,
    "age_ms": 45
  }
}
```

Responses match the repository monitor:

- `200 {"ok":true}` for an authenticated JSON object
- `400` for malformed JSON
- `401` for a wrong key
- `404` for a wrong path
- `429` after repeated authentication failures from one client
- `GET /` and `GET /health` return the compatible `roadlink-monitor` health response

Bodies over 64 KiB are rejected. The access key is removed before events are logged or sent to the renderer. Accepted events gain `_received_at` and `_client` metadata and are appended to `roadlink-telemetry.jsonl` below the app's data directory.

## Multiple devices

The current physical firmware always sends `"device":"roadlink"`, which is not a unique fleet identity. The dashboard selects an identity in this order:

1. `device_id`
2. `imei`
3. a non-default `device` value
4. client IP as a legacy fallback

The Virtual RoadLink adds `device_id`, `imei`, `firmware`, `sequence`, `captured_at`, modem data, and odometer data without breaking firmware compatibility. Add at least a stable `device_id` or IMEI to the A7670SA firmware payload before connecting several physical RoadLinks. Mobile-carrier NAT makes source IP unsuitable as a durable identity.

## Network paths

- Same PC: use `127.0.0.1`, the configured port, and the displayed key.
- Same LAN: use the dashboard's displayed LAN IPv4 address. Add the Windows Firewall private-network rule from the Receiver panel.
- Cellular/internet: enable **Automatic public port**. The app tries NAT-PMP first, then UPnP IGD, requests a random public TCP port, renews the lease, and removes it on shutdown.

Automatic mapping cannot bypass CGNAT, double NAT, router policy, or an ISP that blocks inbound connections. If the router reports a private WAN address, the app identifies likely CGNAT instead of presenting a misleading endpoint.

## Security boundary

Public mapping is off by default. The six-digit key exists for compatibility, is compared in constant time, and is backed by a per-client failed-authentication limit. It is still only one million possibilities and HTTP is not encrypted. Do not treat this direct receiver as production internet security.

For a deployed fleet, keep the same UI model but replace the direct path with outbound TLS:

```text
RoadLink + A7670SA --HTTPS/MQTT over TLS--> hosted ingest + durable database
                                                |
                                         authenticated stream
                                                |
                                      RoadLink Fleet desktop app
```

Each physical RoadLink should then have a unique secret or client certificate. A hosted service also solves CGNAT, supports several dashboard PCs, preserves history while the PC is off, and provides a real two-way command queue.

## Streaming configuration and heartbeat

The A7670SA firmware sends authenticated `POST /heartbeat` requests approximately
every 30 seconds, independently of telemetry START/STOP. Both heartbeat and
telemetry requests may contain this complete configuration:

```json
{"revision":7,"running":false,"gps":true,"obd":true,"obd_fields":135,"interval_seconds":10}
```

The receiver validates booleans, revision 1–4294967295, field mask 0–511 and
interval 1–359999 seconds. A heartbeat requires a valid `config` object. Legacy
telemetry without `config` remains supported. Heartbeats increment their own
counter and update device presence without creating telemetry events or log rows.

Desktop changes are saved in `streaming-config.json` under Electron's user-data
directory, with a newer revision. On the next authenticated request, the receiver
returns `{"ok":true,"config":{...}}`. The response remains below the firmware's
1536-byte configuration limit. Pending changes survive app restarts and are
resent until the device reports that revision and those values. A deferred START
remains pending while the firmware reports the same settings but running=false.

Newer device-side revisions supersede pending desktop changes. Equal revisions
with different field settings produce a visible conflict and adopt the device's
reported values. Desktop edits include the expected revision so a stale editor
cannot overwrite a newly received configuration. The UI distinguishes saved
intent from the device's actual running state, and indicates offline devices.

The Streaming panel has real desktop controls, not screen mirroring. Its OBD bit
mapping is RPM 0, speed 1, coolant 2, throttle 3, manifold pressure 4, intake
temperature 5, ignition timing 6, ECU voltage 7, fuel level 8. Choices are
retained when an entire module is disabled. The device performs selected PID
polling independently of which menu is displayed.

Physical-device focus does not automatically change the reporting rate. Demo
Mode retains its simulated one-second focus behavior. Unique device IDs, secure
transport, explicit command rejection feedback and physical modem verification
remain follow-up work.
