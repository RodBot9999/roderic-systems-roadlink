const assert = require("node:assert/strict");
const fs = require("node:fs");
const http = require("node:http");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");
const { TelemetryReceiver } = require("../electron/telemetry-server.cjs");

function freePort() {
  return new Promise((resolve, reject) => {
    const server = http.createServer();
    server.once("error", reject);
    server.listen(0, "127.0.0.1", () => {
      const port = server.address().port;
      server.close(() => resolve(port));
    });
  });
}

function request(port, method, pathname, payload) {
  return new Promise((resolve, reject) => {
    const body = payload === undefined ? null : Buffer.from(typeof payload === "string" ? payload : JSON.stringify(payload));
    const outgoing = http.request({
      host: "127.0.0.1",
      port,
      method,
      path: pathname,
      headers: body ? { "Content-Type": "application/json", "Content-Length": body.length } : {},
    }, (response) => {
      const chunks = [];
      response.on("data", (chunk) => chunks.push(chunk));
      response.on("end", () => resolve({ status: response.statusCode, body: JSON.parse(Buffer.concat(chunks).toString("utf8")) }));
    });
    outgoing.on("error", reject);
    if (body) outgoing.write(body);
    outgoing.end();
  });
}

test("receiver authenticates current firmware payload, emits it, and logs without the key", async (context) => {
  const dataDirectory = fs.mkdtempSync(path.join(os.tmpdir(), "roadlink-receiver-"));
  const receiver = new TelemetryReceiver({ dataDirectory });
  const port = await freePort();
  await receiver.updateConfig({ port, accessKey: "123456", autoPortMap: false, enabled: true });
  context.after(async () => {
    await receiver.stop();
    fs.rmSync(dataDirectory, { recursive: true, force: true });
  });

  const eventPromise = new Promise((resolve) => receiver.once("telemetry", resolve));
  const payload = {
    access_key: "123456",
    device: "roadlink",
    device_id: "RL-TEST-01",
    uptime_ms: 12345,
    gps: { valid: true, latitude: 20.6748, longitude: -103.3475, speed_kmh: 42.5 },
    obd: { rpm: 1850, speed_kmh: 43, coolant_c: 91 },
  };
  const response = await request(port, "POST", "/telemetry", payload);
  const event = await eventPromise;

  assert.equal(response.status, 200);
  assert.deepEqual(response.body, { ok: true });
  assert.equal(event.device_id, "RL-TEST-01");
  assert.equal(event.access_key, undefined);
  assert.equal(receiver.state().packetCount, 1);
  const logged = JSON.parse(fs.readFileSync(receiver.state().logPath, "utf8").trim());
  assert.equal(logged.device_id, "RL-TEST-01");
  assert.equal(logged.access_key, undefined);
});

test("receiver exposes health and rejects invalid JSON and credentials", async (context) => {
  const dataDirectory = fs.mkdtempSync(path.join(os.tmpdir(), "roadlink-receiver-"));
  const receiver = new TelemetryReceiver({ dataDirectory });
  const port = await freePort();
  await receiver.updateConfig({ port, accessKey: "654321", enabled: true });
  context.after(async () => {
    await receiver.stop();
    fs.rmSync(dataDirectory, { recursive: true, force: true });
  });

  assert.equal((await request(port, "GET", "/health")).status, 200);
  assert.equal((await request(port, "POST", "/telemetry", "not-json")).status, 400);
  assert.equal((await request(port, "POST", "/telemetry", { access_key: "000000" })).status, 401);
  assert.equal((await request(port, "POST", "/wrong", {})).status, 404);
  assert.equal(receiver.state().packetCount, 0);
});
