const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");
const { TripStore, identity, validPosition, sampleFromTelemetry } = require("../electron/trip-store.cjs");

test("GPS fixes require a valid flag and real coordinates", () => {
  assert.equal(validPosition({ valid: false, latitude: 20, longitude: -103 }), false);
  assert.equal(validPosition({ valid: true, latitude: null, longitude: null }), false);
  assert.equal(validPosition({ valid: true, latitude: 0, longitude: 0 }), false);
  assert.equal(validPosition({ valid: true, latitude: 20, longitude: -103 }), true);
  assert.equal(sampleFromTelemetry({ _received_at: new Date().toISOString(), gps: { valid: false, latitude: 20, longitude: -103 } }).latitude, null);
});

test("stable device ID takes precedence over source IP", () => {
  assert.equal(identity({ device_id: "rl-123", _client: "10.0.0.1" }), "RL-123");
  assert.equal(identity({ imei: "12345", _client: "10.0.0.1" }), "IMEI-12345");
});

test("trip recording survives a new store instance and retains metadata", () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "roadlink-trip-test-"));
  try {
    const store = new TripStore(root);
    const active = store.start({ deviceId: "RL-123", nickname: "Track car", plate: "ABC-123", vehicle: "RoadLink test" });
    store.ingest({ device_id: "OTHER", _received_at: new Date().toISOString(), obd: { speed_kmh: 200 } });
    store.ingest({ device_id: "RL-123", type: "heartbeat", _received_at: new Date().toISOString() });
    store.ingest({ device_id: "RL-123", _received_at: new Date().toISOString(), gps: { valid: false, latitude: null, longitude: null }, obd: { speed_kmh: 35, coolant_c: 91 } });
    store.ingest({ device_id: "RL-123", _received_at: new Date().toISOString(), gps: { valid: true, latitude: 20.1, longitude: -103.1 }, obd: { speed_kmh: 48, coolant_c: 94 } });
    const stopped = store.stop();
    assert.equal(active.deviceId, "RL-123");
    assert.equal(stopped.summary.sampleCount, 2);
    assert.equal(stopped.summary.topSpeedKmh, 48);
    assert.equal(stopped.summary.maxCoolantC, 94);
    const reopened = new TripStore(root);
    assert.equal(reopened.list().length, 1);
    const trip = reopened.get(stopped.id);
    assert.equal(trip.nickname, "Track car");
    assert.equal(trip.plate, "ABC-123");
    assert.equal(trip.samples.length, 2);
    assert.equal(trip.samples[0].latitude, null);
    assert.equal(trip.samples[1].latitude, 20.1);
    assert.match(reopened.root, /trips$/);
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
});
