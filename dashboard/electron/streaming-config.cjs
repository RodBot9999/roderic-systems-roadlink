const fs = require("node:fs");
const path = require("node:path");
const fields = ["running", "gps", "obd", "obd_fields", "interval_seconds"];

function validateStreamingConfig(value) {
  if (!value || typeof value !== "object" || Array.isArray(value) ||
      !Number.isInteger(value.revision) || value.revision < 1 || value.revision > 0xffffffff ||
      !["running", "gps", "obd"].every((key) => typeof value[key] === "boolean") ||
      !Number.isInteger(value.obd_fields) || value.obd_fields < 0 || value.obd_fields > 511 ||
      !Number.isInteger(value.interval_seconds) || value.interval_seconds < 1 || value.interval_seconds > 359999) {
    throw new Error("Invalid streaming configuration");
  }
  return Object.fromEntries(["revision", ...fields].map((key) => [key, value[key]]));
}

function deviceIdentity(payload, client) {
  for (const [key, prefix] of [["device_id", ""], ["imei", "IMEI-"], ["device", ""]]) {
    const value = payload[key];
    if (typeof value === "string" && value.trim() && !(key === "device" && value.toLowerCase() === "roadlink")) {
      if (value.length > 128) throw new Error("Device identifier too long");
      return { id: `${prefix}${value.trim()}`.toUpperCase(), stableIdentity: true };
    }
  }
  return { id: `ROADLINK-${client.replace(/[^a-zA-Z0-9]+/g, "-").replace(/^-|-$/g, "") || "UNKNOWN"}`.toUpperCase(), stableIdentity: false };
}

class StreamingConfigStore {
  constructor(directory) {
    this.file = path.join(directory, "streaming-config.json");
    this.devices = new Map();
    try {
      for (const record of JSON.parse(fs.readFileSync(this.file, "utf8"))) {
        if (typeof record.id !== "string") continue;
        record.applied = validateStreamingConfig(record.applied);
        if (record.desired) record.desired = validateStreamingConfig(record.desired);
        this.devices.set(record.id, record);
      }
    } catch { /* First run or invalid saved file: wait for a fresh report. */ }
  }
  save() {
    fs.writeFileSync(`${this.file}.tmp`, JSON.stringify([...this.devices.values()]), "utf8");
    fs.renameSync(`${this.file}.tmp`, this.file);
  }
  list() { return [...this.devices.values()].map((record) => structuredClone(record)); }
  report(payload, client, heartbeat) {
    if (!payload.config) return null;
    const applied = validateStreamingConfig(payload.config);
    const identity = deviceIdentity(payload, client);
    const previous = this.devices.get(identity.id);
    const record = { ...previous, ...identity, applied,
      lastSeenAt: new Date().toISOString(),
      lastHeartbeatAt: heartbeat ? new Date().toISOString() : previous?.lastHeartbeatAt ?? null,
      status: previous?.status ?? "applied", desired: previous?.desired ?? null };
    if (record.desired && applied.revision >= record.desired.revision) {
      const same = fields.every((field) => applied[field] === record.desired[field]);
      if (same) { record.desired = null; record.status = "applied"; }
      else if (applied.revision > record.desired.revision) {
        record.desired = null; record.status = "changed-on-device";
      } else if (fields.filter((field) => field !== "running").every((field) => applied[field] === record.desired[field])) {
        record.status = "pending"; // Firmware can defer START until the modem is ready.
      } else {
        record.desired = null; record.status = "conflict";
      }
    } else if (!record.desired && previous && applied.revision !== previous.applied.revision) {
      record.status = "changed-on-device";
    }
    this.devices.set(record.id, record);
    try { this.save(); } catch (error) {
      if (previous) this.devices.set(record.id, previous); else this.devices.delete(record.id);
      throw error;
    }
    return record.desired && record.desired.revision > applied.revision ? record.desired : null;
  }
  queue(id, patch, expectedRevision) {
    const record = this.devices.get(id);
    if (!record) throw new Error("Wait for a device heartbeat before configuring it");
    const current = record.desired ?? record.applied;
    if (expectedRevision !== current.revision) throw new Error("Settings changed. Review the latest device configuration and try again.");
    if (!patch || typeof patch !== "object" || Array.isArray(patch) ||
        Object.keys(patch).some((key) => !fields.includes(key))) throw new Error("Invalid configuration fields");
    const desired = validateStreamingConfig({ ...current, ...patch, revision: current.revision + 1 });
    const next = { ...record, desired, status: "pending" };
    this.devices.set(id, next);
    try { this.save(); } catch (error) { this.devices.set(id, record); throw error; }
    return structuredClone(next);
  }
}
module.exports = { StreamingConfigStore, validateStreamingConfig, deviceIdentity };
