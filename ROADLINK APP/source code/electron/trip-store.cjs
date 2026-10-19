const crypto = require("node:crypto");
const fs = require("node:fs");
const path = require("node:path");
const { EventEmitter } = require("node:events");

function safePart(value) {
  return String(value ?? "").trim().replace(/[<>:"/\\|?*\x00-\x1f]/g, "-").replace(/[. ]+$/g, "").slice(0, 64) || "Unknown";
}

function identity(event) {
  if (typeof event.device_id === "string" && event.device_id.trim()) return event.device_id.trim().toUpperCase();
  if (typeof event.imei === "string" && event.imei.trim()) return `IMEI-${event.imei.trim()}`;
  if (typeof event.device === "string" && event.device.trim().toLowerCase() !== "roadlink") return event.device.trim().toUpperCase();
  return `ROADLINK-${String(event._client ?? "UNKNOWN").replace(/[^a-zA-Z0-9]+/g, "-").replace(/^-|-$/g, "")}`.toUpperCase();
}

function validPosition(gps) {
  return gps?.valid === true && Number.isFinite(gps.latitude) && Number.isFinite(gps.longitude)
    && Math.abs(gps.latitude) <= 90 && Math.abs(gps.longitude) <= 180
    && !(gps.latitude === 0 && gps.longitude === 0);
}

function number(value) {
  return typeof value === "number" && Number.isFinite(value) ? value : null;
}

function sampleFromTelemetry(event) {
  const timestamp = Date.parse(event._received_at);
  return {
    timestamp: Number.isFinite(timestamp) ? timestamp : Date.now(),
    latitude: validPosition(event.gps) ? event.gps.latitude : null,
    longitude: validPosition(event.gps) ? event.gps.longitude : null,
    speed: number(event.obd?.speed_kmh) ?? (validPosition(event.gps) ? number(event.gps?.speed_kmh) : null),
    rpm: number(event.obd?.rpm),
    coolant: number(event.obd?.coolant_c),
    voltage: number(event.obd?.voltage_v),
    fuel: number(event.obd?.fuel_pct),
    throttle: number(event.obd?.throttle_pct),
    satellites: number(event.gps?.satellites),
  };
}

function distanceKm(a, b) {
  const radians = Math.PI / 180;
  const dLat = (b.latitude - a.latitude) * radians;
  const dLon = (b.longitude - a.longitude) * radians;
  const h = Math.sin(dLat / 2) ** 2 + Math.cos(a.latitude * radians) * Math.cos(b.latitude * radians) * Math.sin(dLon / 2) ** 2;
  return 12742 * Math.asin(Math.min(1, Math.sqrt(h)));
}

function summary(samples, startedAt, endedAt) {
  let distance = 0;
  let previousFix = null;
  for (const sample of samples) {
    if (sample.latitude == null || sample.longitude == null) { previousFix = null; continue; }
    if (previousFix) {
      const seconds = (sample.timestamp - previousFix.timestamp) / 1000;
      const segment = distanceKm(previousFix, sample);
      if (seconds > 0 && seconds <= 300 && segment / seconds * 3600 <= 220) distance += segment;
    }
    previousFix = sample;
  }
  const maximum = (field) => samples.reduce((max, item) => item[field] == null ? max : Math.max(max, item[field]), 0);
  return {
    durationSeconds: Math.max(0, Math.round((endedAt - startedAt) / 1000)),
    distanceKm: Math.round(distance * 100) / 100,
    topSpeedKmh: Math.round(maximum("speed")),
    maxRpm: Math.round(maximum("rpm")),
    maxCoolantC: Math.round(maximum("coolant")),
    gpsPoints: samples.filter((item) => item.latitude != null).length,
    sampleCount: samples.length,
  };
}

class TripStore extends EventEmitter {
  constructor(dataDirectory) {
    super();
    this.root = path.join(dataDirectory, "trips");
    fs.mkdirSync(this.root, { recursive: true });
    this.active = null;
  }

  activeTrip() {
    if (!this.active) return null;
    const { id, deviceId, nickname, plate, vehicle, startedAt, samples } = this.active;
    return { id, deviceId, nickname, plate, vehicle, startedAt, summary: summary(samples, startedAt, Date.now()) };
  }

  start(details) {
    if (this.active) throw new Error("End the current trip before starting another");
    const deviceId = String(details?.deviceId ?? "").trim().toUpperCase();
    if (!/^[A-Z0-9][A-Z0-9._:-]{0,79}$/.test(deviceId)) throw new Error("Select a valid RoadLink device");
    const startedAt = Date.now();
    const id = `${new Date(startedAt).toISOString().replace(/[:.]/g, "-")}-${crypto.randomBytes(3).toString("hex")}`;
    const nickname = String(details.nickname ?? deviceId).trim().slice(0, 100);
    const plate = String(details.plate ?? "").trim().slice(0, 50);
    const vehicle = String(details.vehicle ?? "").trim().slice(0, 100);
    const folder = path.join(this.root, `${safePart(deviceId)} - ${safePart(nickname)} - ${safePart(plate)}`, id);
    fs.mkdirSync(folder, { recursive: true });
    this.active = { id, deviceId, nickname, plate, vehicle, startedAt, folder, samples: [] };
    this.emit("change", this.activeTrip());
    return this.activeTrip();
  }

  ingest(event) {
    if (!this.active || identity(event) !== this.active.deviceId || event.type === "heartbeat") return;
    const sample = sampleFromTelemetry(event);
    if (sample.latitude == null && sample.speed == null && sample.rpm == null && sample.coolant == null) return;
    fs.appendFileSync(path.join(this.active.folder, "samples.jsonl"), `${JSON.stringify(sample)}\n`, "utf8");
    this.active.samples.push(sample);
    this.emit("change", this.activeTrip());
  }

  stop() {
    if (!this.active) throw new Error("No trip is recording");
    const trip = this.active;
    const endedAt = Date.now();
    const manifest = {
      version: 1, id: trip.id, deviceId: trip.deviceId, nickname: trip.nickname,
      plate: trip.plate, vehicle: trip.vehicle, startedAt: trip.startedAt, endedAt,
      summary: summary(trip.samples, trip.startedAt, endedAt),
    };
    fs.writeFileSync(path.join(trip.folder, "trip.json"), `${JSON.stringify(manifest, null, 2)}\n`, "utf8");
    this.active = null;
    this.emit("change", null);
    return manifest;
  }

  list() {
    const trips = [];
    for (const deviceFolder of fs.readdirSync(this.root, { withFileTypes: true })) {
      if (!deviceFolder.isDirectory()) continue;
      const devicePath = path.join(this.root, deviceFolder.name);
      for (const tripFolder of fs.readdirSync(devicePath, { withFileTypes: true })) {
        if (!tripFolder.isDirectory()) continue;
        try {
          const manifest = JSON.parse(fs.readFileSync(path.join(devicePath, tripFolder.name, "trip.json"), "utf8"));
          if (manifest.version === 1) trips.push(manifest);
        } catch { /* Incomplete or malformed trip; keep its files for recovery. */ }
      }
    }
    return trips.sort((a, b) => b.startedAt - a.startedAt);
  }

  get(id) {
    if (typeof id !== "string" || !/^[0-9TZ-]+-[a-f0-9]{6}$/.test(id)) throw new Error("Invalid trip ID");
    for (const deviceFolder of fs.readdirSync(this.root, { withFileTypes: true })) {
      if (!deviceFolder.isDirectory()) continue;
      const tripPath = path.join(this.root, deviceFolder.name, id);
      if (!fs.existsSync(path.join(tripPath, "trip.json"))) continue;
      const manifest = JSON.parse(fs.readFileSync(path.join(tripPath, "trip.json"), "utf8"));
      const samplesPath = path.join(tripPath, "samples.jsonl");
      const samples = fs.existsSync(samplesPath) ? fs.readFileSync(samplesPath, "utf8").split("\n").filter(Boolean).map((line) => JSON.parse(line)) : [];
      return { ...manifest, samples };
    }
    throw new Error("Trip not found");
  }
}

module.exports = { TripStore, identity, validPosition, sampleFromTelemetry, summary };
