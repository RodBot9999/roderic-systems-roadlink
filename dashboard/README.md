# RoadLink Fleet

RoadLink Fleet is a Windows desktop dashboard for monitoring multiple RoadLink vehicle telemetry units. It is an Electron + React application so the interface stays approachable, the code remains easy to extend, and the application can be packaged as a normal Windows `.exe`.

Version 0.2.0 adds a real, firmware-compatible telemetry receiver and a separate Python Virtual RoadLink. The dashboard remains a normal Electron Windows application; networking and privileged operations run outside the renderer.

## What works now

- Live multi-vehicle map using open OpenStreetMap data and CARTO tiles
- Optional MapTiler satellite imagery configured from Preferences
- Authenticated HTTP `POST /telemetry` receiver compatible with the current firmware
- Exact receiver IP, TCP port, and six-digit access key shown inside the app
- Optional NAT-PMP then UPnP public-port mapping with lease renewal and cleanup
- Explicit Windows Firewall private-network rule action with UAC
- Append-only JSONL telemetry logging with the access key removed
- Real live-device discovery, GPS paths, maps, metrics, and charts
- Separate standard-library Python Virtual RoadLink with GUI and headless modes
- End-to-end simulator-to-receiver integration test over a real loopback TCP connection
- Demo Mode can be disabled completely without deleting the saved demo fleet
- Guadalajara-centered simulated vehicles and routes
- Fleet list with online, moving, parked, and offline states
- Editable device nickname, assigned vehicle, license plate, and normal reporting rate
- Automatic 1-second high-detail mode when a RoadLink is focused
- Per-device speed, RPM, coolant, battery, fuel, LTE signal, GPS, and odometer data
- Speed, RPM, and coolant history charts
- Simulated moving telemetry and map positions
- Add/pair-device workflow for creating fleet records before hardware arrives
- Local preferences for map appearance, accent color, display density, and speed units
- Local persistence for fleet records and preferences
- Windows installer packaging

## Important current boundary

The direct receiver now communicates with the existing RoadLink HTTP protocol. The new A7670SA firmware adapter is still pending the physical module and its exact firmware revision. Current RoadLink firmware does not send a unique device ID and does not accept reporting-rate commands, so those two fields must be added for a reliable multi-device, two-way fleet. The implemented contract, network behavior, and production migration path are documented in [`docs/TELEMETRY_PROTOCOL.md`](docs/TELEMETRY_PROTOCOL.md).

## Project structure

```text
electron/
  main.cjs                Window lifecycle, narrow IPC, and firewall action
  preload.cjs             Isolated typed desktop bridge
  telemetry-server.cjs    HTTP authentication, logging, and receiver state
  port-mapper.cjs         NAT-PMP and UPnP mapping lifecycle
src/
  App.tsx                 Fleet state, receiver controls, map, and charts
  main.tsx                React entry point
  styles.css              Visual system and responsive desktop layout
../tools/virtual_roadlink/
  virtual_roadlink.py     Standalone simulator GUI, CLI, drive model, transport
  virtual_roadlink.pyw    Double-click Windows launcher
  run_virtual_roadlink.bat
  test_virtual_roadlink.py
tests/
  telemetry-server.test.cjs
  simulator-e2e.test.cjs
docs/
  TELEMETRY_PROTOCOL.md
```

The first version deliberately keeps the interface in one feature module. When real ingestion is added, the natural split is `features/fleet`, `features/devices`, `features/telemetry`, `services/api`, and `services/live`.

## Development

Requirements: Node.js 22 or newer and pnpm.

```powershell
pnpm install
pnpm dev
```

Satellite view can be configured inside the app, or a development default can be supplied through `VITE_MAPTILER_KEY` in a local `.env` file. Never commit a production key to the repository.

Production UI build:

```powershell
pnpm run build
```

Protocol and receiver tests:

```powershell
pnpm test
py -3 -m unittest ..\tools\virtual_roadlink\test_virtual_roadlink.py -v
```

Windows installer:

```powershell
pnpm run dist:win
```

Installer output is written to `release/`.

## Security choices

- Electron context isolation is enabled.
- Renderer Node.js integration is disabled.
- The renderer is sandboxed.
- External links are opened by the operating system instead of inside the application.
- Editable values are added to map tooltips as text nodes, not injected HTML.
- Receiver sockets, configuration, logs, router mapping, and firewall actions are owned by the Electron main process.
- The key is stripped before telemetry reaches the renderer or log.
- Failed key attempts are rate-limited per client.
- Public router mapping is opt-in and disabled by default.
- The compatible six-digit HTTP key remains prototype security; a hosted fleet should use TLS and per-device credentials.

## Suggested next milestones

1. Add a stable device ID/IMEI and A7670SA transport to the physical firmware.
2. Add a hosted TLS ingest service for CGNAT-safe deployments and durable history.
3. Implement device command acknowledgements and focus-mode expiry.
4. Persist trip history in a queryable local or hosted database and add playback/export.
5. Add signed releases, automatic updates, and a RoadLink application icon.

## License

MIT
