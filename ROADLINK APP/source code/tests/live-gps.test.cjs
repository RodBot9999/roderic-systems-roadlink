const assert = require("node:assert/strict");
const test = require("node:test");

test("a GPS fix survives a GPS-less heartbeat without dereferencing missing coordinates", async () => {
  const { liveGpsPosition } = await import("../src/live-gps.ts");
  const fix = liveGpsPosition({ gps: { valid: true, latitude: 20.6748, longitude: -103.3475 } });
  assert.deepEqual(fix, { hasGpsFix: true, latitude: 20.6748, longitude: -103.3475 });
  assert.deepEqual(liveGpsPosition({ type: "heartbeat" }, fix), fix);
  assert.deepEqual(liveGpsPosition({}, fix), { ...fix, hasGpsFix: false });
  assert.deepEqual(liveGpsPosition({ gps: { valid: true, latitude: 100, longitude: 0 } }, fix), { ...fix, hasGpsFix: false });
});
