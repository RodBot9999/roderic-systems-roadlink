const assert = require("node:assert/strict");
const fs = require("node:fs");
const http = require("node:http");
const os = require("node:os");
const path = require("node:path");
const { execFile } = require("node:child_process");
const test = require("node:test");
const { promisify } = require("node:util");
const { TelemetryReceiver } = require("../electron/telemetry-server.cjs");

const execFileAsync = promisify(execFile);

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

test("standalone Python simulator crosses the real HTTP receiver boundary", {
  skip: !process.env.ROADLINK_PYTHON,
}, async (context) => {
  const dataDirectory = fs.mkdtempSync(path.join(os.tmpdir(), "roadlink-e2e-"));
  const receiver = new TelemetryReceiver({ dataDirectory });
  const port = await freePort();
  await receiver.updateConfig({ port, accessKey: "246810", enabled: true });
  context.after(async () => {
    await receiver.stop();
    fs.rmSync(dataDirectory, { recursive: true, force: true });
  });

  const telemetry = new Promise((resolve) => receiver.once("telemetry", resolve));
  const simulatorPath = path.join(__dirname, "..", "..", "tools", "virtual_roadlink", "virtual_roadlink.py");
  const processResult = await execFileAsync(process.env.ROADLINK_PYTHON, [
    simulatorPath,
    "--once",
    "--host", "127.0.0.1",
    "--port", String(port),
    "--access-key", "246810",
    "--device-id", "RL-E2E-01",
    "--imei", "867997069991234",
  ], { timeout: 15_000, windowsHide: true });
  const event = await telemetry;

  assert.match(processResult.stdout, /HTTP 200/);
  assert.equal(event.device_id, "RL-E2E-01");
  assert.equal(event.modem.technology, "LTE Cat-1 (virtual A7670SA)");
  assert.equal(receiver.state().packetCount, 1);
});
