# RoadLink Desktop Telemetry Monitor

The Windows monitor receives authenticated SIM800L telemetry, displays live GPS
and OBD-II values, and logs accepted packets as JSONL.

## Normal use

Double-click `RoadLinkMonitor.exe`. At startup it:

1. listens locally on TCP port 8080;
2. adds a Windows Firewall rule;
3. requests a one-hour temporary TCP mapping from the router using NAT-PMP or
   UPnP IGD;
4. shows the public IPv4 address, public port, and six-digit access key;
5. renews the temporary lease while the app remains open.

Enter those three displayed values on the device under
`Settings > SIM Configuration`. Closing the app deletes both the router mapping
and firewall rule. If the app or PC stops unexpectedly, the router lease expires.

Windows may show a UAC prompt because changing the firewall requires
Administrator permission.

## Router limitations

Automatic mapping requires NAT-PMP or UPnP to be enabled on the router. It
cannot bypass carrier-grade NAT (CGNAT), double NAT, a cellular hotspot that
blocks inbound traffic, or an ISP that filters the chosen port. The app reports
these failures instead of displaying a false public endpoint.

This replaces the previous tunnel workflow; no Pinggy or tunnel URL is used.

## Access key

The monitor generates `roadlink-monitor.json` beside the app on first run and
stores its six-digit key there. Only requests containing the matching
`access_key` are accepted. Keep the key private and close the app when testing is
finished.

## Source and test modes

```powershell
python roadlink_monitor.py
python roadlink_monitor.py --no-port-map
python roadlink_monitor.py --headless --no-port-map --access-key 123456
python -m unittest -v test_receiver.py
```

`--no-port-map` is useful for loopback/LAN testing.
