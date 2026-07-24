#!/usr/bin/env python3
"""RoadLink SIM800L telemetry receiver and desktop monitor."""

from __future__ import annotations

import argparse
import json
import queue
import secrets
import subprocess
import sys
import threading
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
import tkinter as tk
from tkinter import ttk

from port_mapper import AutoPortMapper


MAX_BODY_BYTES = 64 * 1024
FIREWALL_RULE = "Roderic Systems RoadLink Monitor"


def app_data_directory() -> Path:
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parent


def load_access_key(config_path: Path, override: str | None = None) -> str:
    if override is not None:
        if not (len(override) == 6 and override.isdigit()):
            raise ValueError("Access key must contain exactly six digits")
        return override
    try:
        saved = json.loads(config_path.read_text(encoding="utf-8"))
        key = str(saved.get("access_key", ""))
        if len(key) == 6 and key.isdigit():
            return key
    except (OSError, ValueError, TypeError):
        pass
    key = f"{secrets.randbelow(1_000_000):06d}"
    config_path.write_text(
        json.dumps({"access_key": key}, indent=2) + "\n", encoding="utf-8"
    )
    return key


def set_firewall_rule(port: int, enabled: bool) -> None:
    flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    subprocess.run(
        ["netsh", "advfirewall", "firewall", "delete", "rule",
         f"name={FIREWALL_RULE}"],
        capture_output=True, creationflags=flags, check=False,
    )
    if enabled:
        result = subprocess.run(
            ["netsh", "advfirewall", "firewall", "add", "rule",
             f"name={FIREWALL_RULE}", "dir=in", "action=allow",
             "protocol=TCP", f"localport={port}"],
            capture_output=True, text=True, creationflags=flags, check=False,
        )
        if result.returncode:
            raise RuntimeError("Windows firewall rule requires Administrator access")


class TelemetryReceiver:
    def __init__(self, access_key: str, log_path: Path | None = None) -> None:
        self.events: queue.Queue[dict[str, Any]] = queue.Queue()
        self.access_key = access_key
        self.log_path = log_path
        self._log_lock = threading.Lock()

    def accept(self, payload: dict[str, Any], client: str) -> None:
        event = dict(payload)
        event.pop("access_key", None)
        event["_received_at"] = datetime.now(timezone.utc).isoformat()
        event["_client"] = client
        self.events.put(event)
        if self.log_path is not None:
            self.log_path.parent.mkdir(parents=True, exist_ok=True)
            line = json.dumps(event, separators=(",", ":"), ensure_ascii=False)
            with self._log_lock:
                with self.log_path.open("a", encoding="utf-8") as output:
                    output.write(line + "\n")


class TelemetryRequestHandler(BaseHTTPRequestHandler):
    server_version = "RoadLinkMonitor/2.0"

    def do_POST(self) -> None:  # noqa: N802
        if self.path.rstrip("/") != "/telemetry":
            self._send_json(404, {"ok": False, "error": "Use POST /telemetry"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self._send_json(400, {"ok": False, "error": "Invalid Content-Length"})
            return
        if length <= 0 or length > MAX_BODY_BYTES:
            self._send_json(413, {"ok": False, "error": "Invalid body size"})
            return
        try:
            payload = json.loads(self.rfile.read(length).decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            self._send_json(400, {"ok": False, "error": "Body must be UTF-8 JSON"})
            return
        if not isinstance(payload, dict):
            self._send_json(400, {"ok": False, "error": "JSON root must be an object"})
            return
        receiver: TelemetryReceiver = self.server.receiver  # type: ignore[attr-defined]
        if not secrets.compare_digest(
            str(payload.get("access_key", "")), receiver.access_key
        ):
            self._send_json(401, {"ok": False, "error": "Invalid access key"})
            return
        receiver.accept(payload, self.client_address[0])
        self._send_json(200, {"ok": True})

    def do_GET(self) -> None:  # noqa: N802
        if self.path.rstrip("/") in ("", "/health"):
            self._send_json(200, {"ok": True, "service": "roadlink-monitor"})
        else:
            self._send_json(404, {"ok": False, "error": "Not found"})

    def log_message(self, format_string: str, *args: Any) -> None:
        return

    def _send_json(self, status: int, payload: dict[str, Any]) -> None:
        body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class RoadLinkHttpServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address: tuple[str, int], receiver: TelemetryReceiver) -> None:
        self.receiver = receiver
        super().__init__(address, TelemetryRequestHandler)


class ServerController:
    def __init__(self, host: str, port: int, receiver: TelemetryReceiver) -> None:
        self.server = RoadLinkHttpServer((host, port), receiver)
        self.thread = threading.Thread(
            target=self.server.serve_forever, name="roadlink-http", daemon=True
        )

    @property
    def port(self) -> int:
        return int(self.server.server_address[1])

    def start(self) -> None:
        self.thread.start()

    def stop(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)


def display_value(value: Any, unit: str = "") -> str:
    return "--" if value is None or value == "" else f"{value}{unit}"


class RoadLinkMonitorApp:
    def __init__(
        self, root: tk.Tk, receiver: TelemetryReceiver, server: ServerController,
        bind_host: str, access_key: str, mapper: AutoPortMapper | None,
        mapping_messages: queue.Queue[str],
    ) -> None:
        self.root = root
        self.receiver = receiver
        self.server = server
        self.mapper = mapper
        self.mapping_messages = mapping_messages
        self.records: list[dict[str, Any]] = []
        self.values: dict[str, tk.StringVar] = {}
        self.mapping_status = tk.StringVar(
            value="Local-only mode" if mapper is None else "Opening public endpoint..."
        )
        self.public_endpoint = tk.StringVar(value="Waiting for router...")

        root.title("Roderic Systems RoadLink Monitor")
        root.geometry("1040x750")
        root.minsize(860, 620)
        root.protocol("WM_DELETE_WINDOW", self.close)
        style = ttk.Style(root)
        if "clam" in style.theme_names():
            style.theme_use("clam")
        style.configure("Title.TLabel", font=("Segoe UI", 18, "bold"))
        style.configure("Metric.TLabel", font=("Consolas", 12, "bold"))

        header = ttk.Frame(root, padding=12)
        header.pack(fill="x")
        ttk.Label(
            header, text="RODERIC SYSTEMS // ROADLINK", style="Title.TLabel"
        ).pack(side="left")

        connection = ttk.LabelFrame(root, text="ESP32 cellular connection", padding=10)
        connection.pack(fill="x", padx=12, pady=(0, 10))
        ttk.Label(connection, text="Public IP / port").grid(row=0, column=0, sticky="w")
        ttk.Label(
            connection, textvariable=self.public_endpoint, style="Metric.TLabel"
        ).grid(row=0, column=1, sticky="w", padx=(12, 30))
        ttk.Label(connection, text="Access key").grid(row=0, column=2, sticky="w")
        ttk.Label(
            connection, text=access_key, style="Metric.TLabel"
        ).grid(row=0, column=3, sticky="w", padx=(12, 0))
        ttk.Label(connection, textvariable=self.mapping_status).grid(
            row=1, column=0, columnspan=4, sticky="w", pady=(5, 0)
        )
        ttk.Label(
            connection, text=f"Local receiver: {bind_host}:{server.port}/telemetry"
        ).grid(row=2, column=0, columnspan=4, sticky="w")

        content = ttk.Panedwindow(root, orient="horizontal")
        content.pack(fill="both", expand=True, padx=12, pady=(0, 12))
        metrics = ttk.Frame(content, padding=8)
        history = ttk.Frame(content, padding=8)
        content.add(metrics, weight=2)
        content.add(history, weight=3)
        self._build_metrics(metrics)
        self._build_history(history)
        self.status = tk.StringVar(value="Waiting for RoadLink telemetry...")
        ttk.Label(root, textvariable=self.status, padding=(12, 4)).pack(fill="x")
        self.root.after(100, self.poll)

    def _build_metrics(self, parent: ttk.Frame) -> None:
        groups = (
            ("GPS", (
                ("GPS fix", "gps.valid", ""), ("Latitude", "gps.latitude", ""),
                ("Longitude", "gps.longitude", ""),
                ("GPS speed", "gps.speed_kmh", " km/h"),
                ("Altitude", "gps.altitude_m", " m"),
                ("Satellites", "gps.satellites", ""),
            )),
            ("OBD-II", (
                ("Engine RPM", "obd.rpm", " rpm"),
                ("Vehicle speed", "obd.speed_kmh", " km/h"),
                ("Coolant", "obd.coolant_c", " °C"),
                ("Throttle", "obd.throttle_pct", " %"),
                ("Module voltage", "obd.voltage_v", " V"),
                ("Fuel", "obd.fuel_pct", " %"),
            )),
        )
        for title, fields in groups:
            box = ttk.LabelFrame(parent, text=title, padding=10)
            box.pack(fill="x", pady=(0, 10))
            for label, key, unit in fields:
                row = ttk.Frame(box)
                row.pack(fill="x", pady=2)
                ttk.Label(row, text=label).pack(side="left")
                value = tk.StringVar(value="--")
                value._roadlink_unit = unit  # type: ignore[attr-defined]
                ttk.Label(row, textvariable=value, style="Metric.TLabel").pack(
                    side="right"
                )
                self.values[key] = value

    def _build_history(self, parent: ttk.Frame) -> None:
        ttk.Label(parent, text="Received packets").pack(anchor="w")
        columns = ("time", "client", "rpm", "speed", "position")
        self.table = ttk.Treeview(parent, columns=columns, show="headings", height=12)
        for name, heading, width in (
            ("time", "Received", 145), ("client", "Client", 110),
            ("rpm", "RPM", 70), ("speed", "OBD km/h", 80),
            ("position", "GPS position", 190),
        ):
            self.table.heading(name, text=heading)
            self.table.column(name, width=width, anchor="w")
        self.table.pack(fill="both", expand=True, pady=(4, 10))
        ttk.Label(parent, text="Latest JSON").pack(anchor="w")
        self.raw = tk.Text(parent, height=12, font=("Consolas", 9), wrap="none")
        self.raw.pack(fill="both", expand=True, pady=(4, 0))
        self.raw.configure(state="disabled")

    def poll(self) -> None:
        while True:
            try:
                self.mapping_status.set(self.mapping_messages.get_nowait())
            except queue.Empty:
                break
        if self.mapper is not None and self.mapper.result is not None:
            result = self.mapper.result
            self.public_endpoint.set(f"{result.public_ip} : {result.public_port}")
        received = False
        while True:
            try:
                event = self.receiver.events.get_nowait()
            except queue.Empty:
                break
            self._show_event(event)
            received = True
        if received:
            self.status.set(
                f"Received {len(self.records)} packet(s); latest packet accepted."
            )
        self.root.after(100, self.poll)

    def _show_event(self, event: dict[str, Any]) -> None:
        self.records.append(event)
        gps = event.get("gps") if isinstance(event.get("gps"), dict) else {}
        obd = event.get("obd") if isinstance(event.get("obd"), dict) else {}
        for key, variable in self.values.items():
            group, field = key.split(".", 1)
            value = (gps if group == "gps" else obd).get(field)
            if field == "valid":
                value = "VALID" if value else "NO FIX"
            variable.set(display_value(value, getattr(variable, "_roadlink_unit", "")))
        received_at = str(event.get("_received_at", ""))
        try:
            time_text = datetime.fromisoformat(received_at).astimezone().strftime(
                "%Y-%m-%d %H:%M:%S"
            )
        except ValueError:
            time_text = received_at
        position = (
            f"{gps.get('latitude')}, {gps.get('longitude')}"
            if gps.get("latitude") is not None and gps.get("longitude") is not None
            else "--"
        )
        self.table.insert(
            "", 0, values=(time_text, event.get("_client", "--"),
                           display_value(obd.get("rpm")),
                           display_value(obd.get("speed_kmh")), position)
        )
        for item in self.table.get_children()[100:]:
            self.table.delete(item)
        self.raw.configure(state="normal")
        self.raw.delete("1.0", "end")
        self.raw.insert("1.0", json.dumps(event, indent=2, ensure_ascii=False))
        self.raw.configure(state="disabled")

    def close(self) -> None:
        if self.mapper is not None:
            self.mapping_status.set("Removing temporary router mapping...")
            self.root.update_idletasks()
            self.mapper.stop()
        try:
            set_firewall_rule(self.server.port, False)
        except Exception:
            pass
        self.server.stop()
        self.root.destroy()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="0.0.0.0", help="Address to bind")
    parser.add_argument("--port", type=int, default=8080, help="HTTP port")
    parser.add_argument(
        "--log", type=Path,
        help="JSONL log file",
    )
    parser.add_argument("--headless", action="store_true")
    parser.add_argument("--no-port-map", action="store_true")
    parser.add_argument("--access-key", help="Exactly six digits")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    access_key = load_access_key(
        app_data_directory() / "roadlink-monitor.json", args.access_key
    )
    log_path = args.log or (app_data_directory() / "roadlink-telemetry.jsonl")
    receiver = TelemetryReceiver(access_key, log_path)
    server = ServerController(args.host, args.port, receiver)
    server.start()
    print(f"Receiver: http://{args.host}:{server.port}/telemetry")
    print(f"Access key: {access_key}")

    mapping_messages: queue.Queue[str] = queue.Queue()
    mapper: AutoPortMapper | None = None
    if not args.no_port_map:
        try:
            set_firewall_rule(server.port, True)
        except RuntimeError as error:
            mapping_messages.put(f"Warning: {error}")
        mapper = AutoPortMapper(server.port, mapping_messages.put)
        mapper.start()

    if args.headless:
        try:
            server.thread.join()
        except KeyboardInterrupt:
            if mapper is not None:
                mapper.stop()
            server.stop()
        return 0

    root = tk.Tk()
    RoadLinkMonitorApp(
        root, receiver, server, args.host, access_key, mapper, mapping_messages
    )
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
