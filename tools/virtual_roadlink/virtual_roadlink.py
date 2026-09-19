#!/usr/bin/env python3
"""A standalone virtual RoadLink that sends real firmware-compatible HTTP telemetry."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import math
import os
import queue
import random
import sys
import threading
import time
import tkinter as tk
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from tkinter import messagebox, ttk
from urllib.parse import urlparse


APP_NAME = "RoadLink Virtual Device"
DEFAULT_ROUTE = [
    (20.67477, -103.38762),
    (20.67538, -103.38128),
    (20.67605, -103.37405),
    (20.67672, -103.36692),
    (20.67743, -103.35933),
    (20.67753, -103.35031),
    (20.67248, -103.34667),
    (20.66613, -103.34763),
    (20.66018, -103.35042),
    (20.65882, -103.35963),
    (20.66179, -103.37076),
    (20.66787, -103.38133),
]


def config_path() -> Path:
    root = Path(os.environ.get("APPDATA", Path.home())) / APP_NAME
    root.mkdir(parents=True, exist_ok=True)
    return root / "config.json"


def load_config() -> dict[str, object]:
    defaults: dict[str, object] = {
        "host": "127.0.0.1",
        "port": 8080,
        "access_key": "000000",
        "device_id": "RL-VIRTUAL-01",
        "imei": "867997069990001",
        "interval": 2.0,
    }
    try:
        loaded = json.loads(config_path().read_text(encoding="utf-8"))
        if isinstance(loaded, dict):
            defaults.update(loaded)
    except (OSError, ValueError):
        pass
    return defaults


def save_config(config: dict[str, object]) -> None:
    config_path().write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")


def haversine_m(start: tuple[float, float], end: tuple[float, float]) -> float:
    lat1, lon1 = map(math.radians, start)
    lat2, lon2 = map(math.radians, end)
    dlat = lat2 - lat1
    dlon = lon2 - lon1
    value = math.sin(dlat / 2) ** 2 + math.cos(lat1) * math.cos(lat2) * math.sin(dlon / 2) ** 2
    return 6_371_000 * 2 * math.atan2(math.sqrt(value), math.sqrt(1 - value))


def bearing_deg(start: tuple[float, float], end: tuple[float, float]) -> float:
    lat1, lon1 = map(math.radians, start)
    lat2, lon2 = map(math.radians, end)
    y = math.sin(lon2 - lon1) * math.cos(lat2)
    x = math.cos(lat1) * math.sin(lat2) - math.sin(lat1) * math.cos(lat2) * math.cos(lon2 - lon1)
    return (math.degrees(math.atan2(y, x)) + 360) % 360


@dataclass
class VehicleState:
    latitude: float
    longitude: float
    speed_kmh: float = 0.0
    rpm: int = 760
    coolant_c: float = 28.0
    fuel_pct: float = 78.0
    voltage_v: float = 14.1
    throttle_pct: float = 0.0
    course_deg: float = 0.0
    satellites: int = 11
    rssi: int = 23
    odometer_km: float = 84_631.2


class GuadalajaraDrive:
    """A continuous road-like route with acceleration, traffic lights, and engine dynamics."""

    def __init__(self, seed: int = 1007) -> None:
        self.random = random.Random(seed)
        self.route = DEFAULT_ROUTE
        self.leg = 0
        self.leg_progress_m = 0.0
        self.elapsed = 0.0
        self.state = VehicleState(*self.route[0])

    def step(self, elapsed_seconds: float) -> VehicleState:
        elapsed_seconds = max(0.05, min(10.0, elapsed_seconds))
        self.elapsed += elapsed_seconds
        traffic_cycle = self.elapsed % 58.0
        stopped_for_light = 42.0 <= traffic_cycle <= 50.0
        urban_wave = 39.0 + 12.0 * math.sin(self.elapsed / 17.0) + 5.0 * math.sin(self.elapsed / 5.7)
        target_speed = 0.0 if stopped_for_light else max(18.0, min(64.0, urban_wave))
        delta = target_speed - self.state.speed_kmh
        acceleration_limit = (8.0 if delta > 0 else 15.0) * elapsed_seconds
        self.state.speed_kmh += max(-acceleration_limit, min(acceleration_limit, delta))
        self.state.speed_kmh = max(0.0, self.state.speed_kmh)

        remaining = self.state.speed_kmh / 3.6 * elapsed_seconds
        while remaining > 0:
            start = self.route[self.leg]
            end = self.route[(self.leg + 1) % len(self.route)]
            length = max(1.0, haversine_m(start, end))
            available = length - self.leg_progress_m
            travelled = min(remaining, available)
            self.leg_progress_m += travelled
            remaining -= travelled
            fraction = min(1.0, self.leg_progress_m / length)
            self.state.latitude = start[0] + (end[0] - start[0]) * fraction
            self.state.longitude = start[1] + (end[1] - start[1]) * fraction
            self.state.course_deg = bearing_deg(start, end)
            if self.leg_progress_m >= length - 0.01:
                self.leg = (self.leg + 1) % len(self.route)
                self.leg_progress_m = 0.0

        speed_ratio = self.state.speed_kmh / max(1.0, target_speed)
        self.state.throttle_pct = max(0.0, min(72.0, 10.0 + max(0.0, delta) * 2.2 + speed_ratio * 12.0))
        gear_factor = 25.0 + 7.0 * math.sin(self.elapsed / 7.0)
        self.state.rpm = round(760 + self.state.speed_kmh * gear_factor + self.state.throttle_pct * 8.0)
        if self.state.speed_kmh < 1.0:
            self.state.rpm = round(758 + 18 * math.sin(self.elapsed * 1.4))
        self.state.coolant_c += (91.0 - self.state.coolant_c) * min(0.025, elapsed_seconds / 240.0)
        self.state.coolant_c += self.random.uniform(-0.04, 0.04)
        distance_km = self.state.speed_kmh / 3600.0 * elapsed_seconds
        self.state.odometer_km += distance_km
        self.state.fuel_pct = max(0.0, self.state.fuel_pct - distance_km * 0.009)
        self.state.voltage_v = 14.05 + 0.10 * math.sin(self.elapsed / 13.0)
        self.state.satellites = max(7, min(15, round(11 + 2 * math.sin(self.elapsed / 31.0))))
        self.state.rssi = max(10, min(30, round(22 + 4 * math.sin(self.elapsed / 27.0))))
        return self.state


class VirtualRoadLink:
    def __init__(self, config: dict[str, object], status_callback=None) -> None:
        self.config = config
        seed = sum(ord(character) for character in str(config["device_id"]))
        self.drive = GuadalajaraDrive(seed)
        self.sequence = 0
        self.started = time.monotonic()
        self.last_step = self.started
        self.status_callback = status_callback or (lambda _kind, _message, _payload=None: None)

    @property
    def endpoint(self) -> str:
        host = str(self.config["host"]).strip()
        port = int(self.config["port"])
        if "://" in host:
            parsed = urlparse(host)
            scheme = parsed.scheme or "http"
            hostname = parsed.hostname or "127.0.0.1"
            resolved_port = parsed.port or port
            return f"{scheme}://{hostname}:{resolved_port}/telemetry"
        return f"http://{host}:{port}/telemetry"

    def payload(self) -> dict[str, object]:
        now_monotonic = time.monotonic()
        state = self.drive.step(now_monotonic - self.last_step)
        self.last_step = now_monotonic
        self.sequence += 1
        now = dt.datetime.now(dt.timezone.utc)
        return {
            "access_key": str(self.config["access_key"]),
            "device": "roadlink",
            "device_id": str(self.config["device_id"]),
            "imei": str(self.config["imei"]),
            "firmware": "Virtual RoadLink 1.0",
            "sequence": self.sequence,
            "captured_at": now.isoformat(timespec="milliseconds").replace("+00:00", "Z"),
            "uptime_ms": round((now_monotonic - self.started) * 1000),
            "gps": {
                "valid": True,
                "latitude": round(state.latitude, 6),
                "longitude": round(state.longitude, 6),
                "altitude_m": round(1566 + 4 * math.sin(self.drive.elapsed / 40), 1),
                "speed_kmh": round(state.speed_kmh, 1),
                "course_deg": round(state.course_deg, 1),
                "satellites": state.satellites,
                "utc_time": now.strftime("%H%M%S.00"),
                "utc_date": now.strftime("%d%m%y"),
            },
            "obd": {
                "rpm": state.rpm,
                "speed_kmh": round(state.speed_kmh),
                "coolant_c": round(state.coolant_c),
                "throttle_pct": round(state.throttle_pct, 1),
                "map_kpa": round(28 + state.throttle_pct * 0.78),
                "intake_c": round(31 + 2 * math.sin(self.drive.elapsed / 35)),
                "timing_deg": round(8 + state.speed_kmh * 0.22, 1),
                "voltage_v": round(state.voltage_v, 2),
                "fuel_pct": round(state.fuel_pct, 1),
                "odometer_km": round(state.odometer_km, 2),
                "age_ms": 45,
            },
            "modem": {
                "technology": "LTE Cat-1 (virtual A7670SA)",
                "rssi": state.rssi,
                "signal_bars": max(1, min(5, math.ceil(state.rssi / 6.2))),
                "operator": "Virtual LTE",
            },
        }

    def send_once(self) -> dict[str, object]:
        payload = self.payload()
        body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        request = urllib.request.Request(
            self.endpoint,
            data=body,
            method="POST",
            headers={"Content-Type": "application/json", "User-Agent": "RoadLink-Virtual/1.0"},
        )
        try:
            with urllib.request.urlopen(request, timeout=12) as response:
                response_body = response.read().decode("utf-8", errors="replace")
                if response.status != 200:
                    raise RuntimeError(f"HTTP {response.status}: {response_body}")
            self.status_callback("sent", f"Packet {self.sequence} accepted (HTTP 200)", payload)
            return payload
        except urllib.error.HTTPError as error:
            detail = error.read().decode("utf-8", errors="replace")
            message = f"HTTP {error.code}: {detail}"
            self.status_callback("error", message, payload)
            raise RuntimeError(message) from error
        except (OSError, urllib.error.URLError) as error:
            message = f"Connection failed: {error}"
            self.status_callback("error", message, payload)
            raise RuntimeError(message) from error


def validate_config(config: dict[str, object]) -> None:
    if not str(config["host"]).strip():
        raise ValueError("Receiver IP or hostname is required")
    port = int(config["port"])
    if not 1 <= port <= 65535:
        raise ValueError("Port must be from 1 to 65535")
    if not (str(config["access_key"]).isdigit() and len(str(config["access_key"])) == 6):
        raise ValueError("Access key must contain exactly six digits")
    if not str(config["device_id"]).strip():
        raise ValueError("Device ID is required")
    if float(config["interval"]) < 0.25:
        raise ValueError("Interval must be at least 0.25 seconds")


class VirtualRoadLinkApp:
    def __init__(self, root: tk.Tk, initial: dict[str, object]) -> None:
        self.root = root
        self.root.title("RoadLink Virtual Device")
        self.root.geometry("760x610")
        self.root.minsize(660, 540)
        self.root.configure(bg="#0b1013")
        self.events: queue.Queue[tuple[str, str, object]] = queue.Queue()
        self.stop_event = threading.Event()
        self.worker: threading.Thread | None = None
        self.packet_count = 0

        self.values = {
            name: tk.StringVar(value=str(initial[name]))
            for name in ("host", "port", "access_key", "device_id", "imei", "interval")
        }
        self.status = tk.StringVar(value="Stopped — configure the endpoint from RoadLink Fleet")
        self.speed = tk.StringVar(value="0 km/h")
        self.rpm = tk.StringVar(value="760 rpm")
        self.position = tk.StringVar(value="20.674770, -103.387620")
        self.packets = tk.StringVar(value="0 accepted")
        self.build_ui()
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        self.root.after(120, self.drain_events)

    def build_ui(self) -> None:
        style = ttk.Style(self.root)
        style.theme_use("clam")
        style.configure("TFrame", background="#0b1013")
        style.configure("Card.TFrame", background="#11191d")
        style.configure("TLabel", background="#0b1013", foreground="#cfd8d4", font=("Segoe UI", 9))
        style.configure("Muted.TLabel", foreground="#6e7b81")
        style.configure("Title.TLabel", foreground="#eef4f1", font=("Segoe UI Semibold", 20))
        style.configure("Value.TLabel", background="#11191d", foreground="#b8f34a", font=("Cascadia Mono", 15, "bold"))
        style.configure("CardLabel.TLabel", background="#11191d", foreground="#6f7d83", font=("Segoe UI Semibold", 8))
        style.configure("TEntry", fieldbackground="#080d10", foreground="#e2e8e5", bordercolor="#263037", insertcolor="#e2e8e5", padding=8)
        style.configure("Accent.TButton", background="#b8f34a", foreground="#091008", bordercolor="#b8f34a", font=("Segoe UI Semibold", 9), padding=(18, 9))
        style.map("Accent.TButton", background=[("active", "#c8ff61"), ("disabled", "#38472d")])
        style.configure("Secondary.TButton", background="#182126", foreground="#c5cdca", bordercolor="#2a353b", font=("Segoe UI Semibold", 9), padding=(14, 9))

        shell = ttk.Frame(self.root, padding=24)
        shell.pack(fill="both", expand=True)
        header = ttk.Frame(shell)
        header.pack(fill="x")
        ttk.Label(header, text="ROADLINK / TEST TOOLS", style="Muted.TLabel").pack(anchor="w")
        ttk.Label(header, text="Virtual RoadLink", style="Title.TLabel").pack(anchor="w", pady=(4, 0))
        ttk.Label(header, text="A separate process that drives Guadalajara and sends real HTTP telemetry.", style="Muted.TLabel").pack(anchor="w", pady=(3, 0))

        form = ttk.Frame(shell, style="Card.TFrame", padding=16)
        form.pack(fill="x", pady=(18, 12))
        fields = [
            ("Receiver IP / host", "host"), ("TCP port", "port"),
            ("Six-digit access key", "access_key"), ("Device ID", "device_id"),
            ("IMEI", "imei"), ("Send interval (seconds)", "interval"),
        ]
        for index, (label, name) in enumerate(fields):
            row, column = divmod(index, 2)
            cell = ttk.Frame(form, style="Card.TFrame")
            cell.grid(row=row, column=column, sticky="ew", padx=(0, 10) if column == 0 else (10, 0), pady=7)
            ttk.Label(cell, text=label.upper(), style="CardLabel.TLabel").pack(anchor="w", pady=(0, 5))
            ttk.Entry(cell, textvariable=self.values[name]).pack(fill="x")
        form.columnconfigure(0, weight=1)
        form.columnconfigure(1, weight=1)

        controls = ttk.Frame(shell)
        controls.pack(fill="x", pady=(0, 14))
        self.start_button = ttk.Button(controls, text="Start driving", style="Accent.TButton", command=self.start)
        self.start_button.pack(side="left")
        self.stop_button = ttk.Button(controls, text="Stop", style="Secondary.TButton", command=self.stop, state="disabled")
        self.stop_button.pack(side="left", padx=8)
        ttk.Label(controls, textvariable=self.status, style="Muted.TLabel").pack(side="left", padx=10)

        metrics = ttk.Frame(shell)
        metrics.pack(fill="x")
        for column, (label, variable) in enumerate((("SPEED", self.speed), ("ENGINE", self.rpm), ("GPS", self.position), ("PACKETS", self.packets))):
            card = ttk.Frame(metrics, style="Card.TFrame", padding=13)
            card.grid(row=0, column=column, sticky="nsew", padx=(0 if column == 0 else 5, 0 if column == 3 else 5))
            ttk.Label(card, text=label, style="CardLabel.TLabel").pack(anchor="w")
            ttk.Label(card, textvariable=variable, style="Value.TLabel", wraplength=190).pack(anchor="w", pady=(7, 0))
            metrics.columnconfigure(column, weight=1)

        log_card = ttk.Frame(shell, style="Card.TFrame", padding=12)
        log_card.pack(fill="both", expand=True, pady=(12, 0))
        ttk.Label(log_card, text="WIRE ACTIVITY", style="CardLabel.TLabel").pack(anchor="w", pady=(0, 7))
        self.log = tk.Text(log_card, height=7, bg="#080d10", fg="#8d9a9f", insertbackground="#ffffff", relief="flat", font=("Cascadia Mono", 8), padx=10, pady=8, state="disabled")
        self.log.pack(fill="both", expand=True)

    def current_config(self) -> dict[str, object]:
        config: dict[str, object] = {name: value.get().strip() for name, value in self.values.items()}
        config["port"] = int(str(config["port"]))
        config["interval"] = float(str(config["interval"]))
        validate_config(config)
        return config

    def start(self) -> None:
        try:
            config = self.current_config()
        except (ValueError, TypeError) as error:
            messagebox.showerror("Invalid configuration", str(error), parent=self.root)
            return
        save_config(config)
        self.stop_event.clear()
        self.start_button.configure(state="disabled")
        self.stop_button.configure(state="normal")
        self.status.set("Connecting…")
        self.worker = threading.Thread(target=self.run_sender, args=(config,), daemon=True)
        self.worker.start()

    def run_sender(self, config: dict[str, object]) -> None:
        roadlink = VirtualRoadLink(config, lambda kind, message, payload=None: self.events.put((kind, message, payload)))
        interval = float(config["interval"])
        while not self.stop_event.is_set():
            started = time.monotonic()
            try:
                roadlink.send_once()
            except RuntimeError:
                pass
            self.stop_event.wait(max(0.0, interval - (time.monotonic() - started)))
        self.events.put(("stopped", "Stopped", None))

    def stop(self) -> None:
        self.stop_event.set()
        self.status.set("Stopping…")
        self.stop_button.configure(state="disabled")

    def write_log(self, message: str) -> None:
        stamp = dt.datetime.now().strftime("%H:%M:%S")
        self.log.configure(state="normal")
        self.log.insert("end", f"{stamp}  {message}\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def drain_events(self) -> None:
        try:
            while True:
                kind, message, payload = self.events.get_nowait()
                self.write_log(message)
                if kind == "sent" and isinstance(payload, dict):
                    self.packet_count += 1
                    gps = payload["gps"]
                    obd = payload["obd"]
                    self.speed.set(f"{obd['speed_kmh']} km/h")
                    self.rpm.set(f"{obd['rpm']} rpm")
                    self.position.set(f"{gps['latitude']:.6f}, {gps['longitude']:.6f}")
                    self.packets.set(f"{self.packet_count} accepted")
                    self.status.set("Driving and reporting")
                elif kind == "error":
                    self.status.set("Send failed — see activity")
                elif kind == "stopped":
                    self.start_button.configure(state="normal")
                    self.stop_button.configure(state="disabled")
                    self.status.set("Stopped")
        except queue.Empty:
            pass
        self.root.after(120, self.drain_events)

    def close(self) -> None:
        self.stop_event.set()
        self.root.destroy()


def parse_args(argv: list[str]) -> argparse.Namespace:
    defaults = load_config()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default=defaults["host"], help="RoadLink Fleet receiver IP or hostname")
    parser.add_argument("--port", type=int, default=defaults["port"])
    parser.add_argument("--access-key", default=defaults["access_key"])
    parser.add_argument("--device-id", default=defaults["device_id"])
    parser.add_argument("--imei", default=defaults["imei"])
    parser.add_argument("--interval", type=float, default=defaults["interval"])
    parser.add_argument("--headless", action="store_true", help="Run without the desktop interface")
    parser.add_argument("--once", action="store_true", help="Send one packet and exit")
    parser.add_argument("--duration", type=float, default=0, help="Stop headless mode after this many seconds; 0 runs until interrupted")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    config: dict[str, object] = {
        "host": args.host,
        "port": args.port,
        "access_key": args.access_key,
        "device_id": args.device_id,
        "imei": args.imei,
        "interval": args.interval,
    }
    try:
        validate_config(config)
    except (ValueError, TypeError) as error:
        print(f"Configuration error: {error}", file=sys.stderr)
        return 2

    if args.headless or args.once:
        roadlink = VirtualRoadLink(config, lambda _kind, message, _payload=None: print(message, flush=True))
        started = time.monotonic()
        try:
            while True:
                loop_started = time.monotonic()
                roadlink.send_once()
                if args.once or (args.duration and time.monotonic() - started >= args.duration):
                    return 0
                time.sleep(max(0.0, args.interval - (time.monotonic() - loop_started)))
        except KeyboardInterrupt:
            return 0
        except RuntimeError as error:
            print(error, file=sys.stderr)
            return 1

    root = tk.Tk()
    VirtualRoadLinkApp(root, config)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
