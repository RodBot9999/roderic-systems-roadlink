const assert = require("node:assert/strict");
const { EventEmitter } = require("node:events");
const test = require("node:test");
const { PinggyTunnel, pinggyArguments } = require("../electron/pinggy-tunnel.cjs");

class FakeStream extends EventEmitter {}
class FakeChild extends EventEmitter {
  constructor() { super(); this.stdout = new FakeStream(); this.stderr = new FakeStream(); this.killed = false; }
  kill() { this.killed = true; queueMicrotask(() => this.emit("exit", 0)); }
}

test("Pinggy uses bounded SSH arguments and publishes firmware-compatible IPv4 and port", async () => {
  let invocation;
  const child = new FakeChild();
  const tunnel = new PinggyTunnel({
    command: "ssh-test",
    spawnProcess: (command, args, options) => { invocation = { command, args, options }; return child; },
    lookup: async () => ({ address: "203.0.113.42", family: 4 }),
  });
  const starting = tunnel.start(8080);
  await Promise.resolve();
  child.stderr.emit("data", Buffer.from("Connected\r\ntcp://demo.a.free.pinggy.link:"));
  child.stdout.emit("data", Buffer.from("23456\r\n"));
  const state = await starting;
  assert.equal(invocation.command, "ssh-test");
  assert.deepEqual(invocation.args, pinggyArguments(8080));
  assert.deepEqual(invocation.options.stdio, ["ignore", "pipe", "pipe"]);
  assert.equal(state.status, "active");
  assert.equal(state.publicHost, "demo.a.free.pinggy.link");
  assert.equal(state.publicIp, "203.0.113.42");
  assert.equal(state.publicPort, 23456);
  assert.ok(Date.parse(state.expiresAt) > Date.parse(state.startedAt));
  await tunnel.stop();
  assert.equal(child.killed, true);
  assert.equal(tunnel.state().status, "stopped");
});

test("Pinggy rejects invalid local ports without spawning SSH", async () => {
  let spawned = false;
  const tunnel = new PinggyTunnel({ spawnProcess: () => { spawned = true; } });
  await assert.rejects(tunnel.start(0), /Invalid receiver port/);
  assert.equal(spawned, false);
});
