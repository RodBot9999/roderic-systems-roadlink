# Virtual RoadLink

This is a standalone, standard-library Python application that behaves like a RoadLink on the network. It drives a continuous route through Guadalajara, simulates GPS, OBD-II, and LTE values, and sends real HTTP `POST /telemetry` requests using the current firmware payload.

## Use the desktop window

1. In RoadLink Fleet, open **Receiver** and copy the receiver IP, TCP port, and six-digit access key.
2. Turn Demo Mode off if you want to see only real inbound packets.
3. Double-click `virtual_roadlink.pyw`, or run `run_virtual_roadlink.bat` if `.pyw` is not associated with Python.
4. Paste the three receiver values, choose a unique device ID, and select **Start driving**.

For a same-PC test, use `127.0.0.1`. To make the simulator genuinely separate, copy this folder to another Windows, Linux, or Raspberry Pi computer and use the dashboard PC's LAN address. Add the Windows Firewall rule from the Receiver panel first. For an internet test, enable automatic public mapping and use the displayed public IP and port.

No packages need to be installed; Python 3.10 or newer is enough.

## Headless examples

One packet:

```powershell
py -3 virtual_roadlink.py --once --host 127.0.0.1 --port 8080 --access-key 123456 --device-id RL-VIRTUAL-01
```

Run continuously from another computer:

```bash
python3 virtual_roadlink.py --headless --host 192.168.1.50 --port 8080 --access-key 123456 --device-id RL-VIRTUAL-02 --interval 2
```

Run multiple copies with different `--device-id` and `--imei` values to test a fleet. The dashboard uses `device_id` as the stable identity. Current physical firmware sends only `"device":"roadlink"`; adding a unique device ID or IMEI to that firmware payload is required to distinguish several physical units reliably.
