const assert = require("node:assert/strict");
const fs = require("node:fs");
const http = require("node:http");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");
const { EventEmitter } = require("node:events");
const { TelemetryReceiver } = require("../electron/telemetry-server.cjs");
const { StreamingConfigStore } = require("../electron/streaming-config.cjs");

class FakeTunnel extends EventEmitter {
  constructor() {
    super();
    this.starts = [];
    this.stops = 0;
    this.value = { status: "stopped", publicHost: null, publicIp: null,
      publicPort: null, startedAt: null, expiresAt: null, lastError: null };
  }
  state() { return { ...this.value }; }
  async start(port) {
    this.starts.push(port);
    this.value = { status: "active", publicHost: "test.pinggy.link", publicIp: "203.0.113.9",
      publicPort: 23456, startedAt: new Date().toISOString(),
      expiresAt: new Date(Date.now() + 3600000).toISOString(), lastError: null };
    this.emit("state", this.state());
    return this.state();
  }
  async stop() {
    this.stops += 1;
    this.value = { ...this.value, status: "stopped", publicHost: null, publicIp: null, publicPort: null };
    this.emit("state", this.state());
    return this.state();
  }
}

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

const baseConfig = { revision: 1, running: false, gps: true, obd: true, obd_fields: 135, interval_seconds: 10 };

test("heartbeats deliver durable settings and confirm application without emitting telemetry", async (context) => {
  const dataDirectory = fs.mkdtempSync(path.join(os.tmpdir(), "roadlink-config-"));
  const receiver = new TelemetryReceiver({ dataDirectory });
  const port = await freePort();
  await receiver.updateConfig({ port, accessKey: "123456", enabled: true });
  context.after(async () => { await receiver.stop(); fs.rmSync(dataDirectory, { recursive: true, force: true }); });
  let telemetryEvents = 0;
  receiver.on("telemetry", () => ++telemetryEvents);
  const report = { access_key: "123456", device: "roadlink", config: baseConfig };
  assert.equal((await request(port, "POST", "/heartbeat", { ...report, access_key: "000000" })).status, 401);
  assert.equal((await request(port, "POST", "/heartbeat", { ...report, config: { ...baseConfig, gps: "yes" } })).status, 400);
  assert.equal(receiver.state().streamingDevices.length, 0);
  assert.equal((await request(port, "POST", "/heartbeat", report)).status, 200);
  const id = receiver.state().streamingDevices[0].id;
  receiver.queueStreamingConfig(id, { running: true, obd_fields: 5, interval_seconds: 42 }, 1);
  const reloaded = new StreamingConfigStore(dataDirectory);
  assert.equal(reloaded.list()[0].desired.interval_seconds, 42);
  assert.throws(() => receiver.queueStreamingConfig(id, { gps: false }, 1), /Settings changed/);
  assert.throws(() => receiver.queueStreamingConfig(id, { interval_seconds: 0 }, 2), /Invalid streaming/);
  const delivery = await request(port, "POST", "/heartbeat", report);
  assert.equal(delivery.body.config.revision, 2);
  assert.equal(delivery.body.config.obd_fields, 5);
  assert.ok(Buffer.byteLength(JSON.stringify(delivery.body)) < 1536);
  assert.deepEqual((await request(port, "POST", "/heartbeat", report)).body, delivery.body);
  assert.equal(receiver.state().streamingDevices[0].status, "pending");
  await request(port, "POST", "/heartbeat", { ...report, config: { ...delivery.body.config, running: false } });
  assert.equal(receiver.state().streamingDevices[0].status, "pending");
  await request(port, "POST", "/heartbeat", { ...report, config: delivery.body.config });
  assert.equal(receiver.state().streamingDevices[0].status, "applied");
  assert.equal(receiver.state().streamingDevices[0].desired, null);
  assert.equal(receiver.state().packetCount, 0);
  assert.equal(telemetryEvents, 0);
  assert.equal(fs.existsSync(receiver.logPath), false);
  receiver.queueStreamingConfig(id, { gps: false }, 2);
  await request(port, "POST", "/telemetry", { ...report, config: { ...delivery.body.config, revision: 4, obd_fields: 128 } });
  assert.equal(receiver.state().streamingDevices[0].status, "changed-on-device");
  assert.equal(receiver.state().streamingDevices[0].applied.obd_fields, 128);
  assert.equal(receiver.state().streamingDevices[0].desired, null);
  assert.equal(receiver.state().packetCount, 1);
});

test("configuration conflicts are visible and device queues remain isolated", (context) => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), "roadlink-config-"));
  context.after(() => fs.rmSync(directory, { recursive: true, force: true }));
  const store = new StreamingConfigStore(directory);
  store.report({ device_id: "RL-1", config: baseConfig }, "127.0.0.1", true);
  store.report({ device_id: "RL-2", config: baseConfig }, "127.0.0.1", true);
  store.queue("RL-1", { gps: false }, 1);
  assert.equal(store.report({ device_id: "RL-2", config: baseConfig }, "127.0.0.1", true), null);
  store.report({ device_id: "RL-1", config: { ...baseConfig, revision: 2, obd_fields: 3 } }, "127.0.0.1", true);
  assert.equal(store.list()[0].status, "conflict");
  assert.equal(store.list()[0].desired, null);
});

test("free tunnel remains independent of router mapping and follows receiver port changes", async (context) => {
  const dataDirectory = fs.mkdtempSync(path.join(os.tmpdir(), "roadlink-tunnel-"));
  const tunnel = new FakeTunnel();
  const receiver = new TelemetryReceiver({ dataDirectory, tunnel });
  const firstPort = await freePort();
  context.after(async () => { await receiver.stop(); fs.rmSync(dataDirectory, { recursive: true, force: true }); });
  await receiver.updateConfig({ port: firstPort, accessKey: "123456", autoPortMap: false, freeTunnel: true, enabled: true });
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepEqual(tunnel.starts, [firstPort]);
  assert.equal(receiver.state().freeTunnel, true);
  assert.equal(receiver.state().autoPortMap, false);
  assert.equal(receiver.state().publicEndpoint, "http://203.0.113.9:23456/telemetry");
  const secondPort = await freePort();
  await receiver.updateConfig({ port: secondPort });
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepEqual(tunnel.starts, [firstPort, secondPort]);
  assert.ok(tunnel.stops >= 1);
  tunnel.value = { ...tunnel.value, status: "error", lastError: "expired" };
  tunnel.emit("state", tunnel.state());
  await receiver.updateConfig({ freeTunnel: true });
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepEqual(tunnel.starts, [firstPort, secondPort, secondPort]);
  await receiver.updateConfig({ freeTunnel: false });
  assert.equal(receiver.state().freeTunnel, false);
  assert.equal(receiver.state().tunnel.status, "stopped");
});
