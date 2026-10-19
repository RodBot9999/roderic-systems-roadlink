const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");

test("preload exposes the heartbeat listener on the receiver API", () => {
  let api;
  const listeners = new Map();
  const electron = {
    clipboard: { writeText() {} },
    contextBridge: {
      exposeInMainWorld(name, value) {
        assert.equal(name, "roadlinkDesktop");
        api = value;
      },
    },
    ipcRenderer: {
      invoke: async () => ({}),
      sendSync(channel) {
        assert.equal(channel, "app:get-version");
        return require("../package.json").version;
      },
      on(channel, listener) { listeners.set(channel, listener); },
      removeListener(channel, listener) {
        if (listeners.get(channel) === listener) listeners.delete(channel);
      },
    },
  };

  const source = fs.readFileSync(path.join(__dirname, "../electron/preload.cjs"), "utf8");
  vm.runInNewContext(source, {
    require(name) {
      assert.equal(name, "electron");
      return electron;
    },
    process: { platform: "win32" },
  }, { filename: "preload.cjs" });

  assert.equal(typeof api.receiver.onHeartbeat, "function");
  assert.equal(api.version, require("../package.json").version);
  assert.equal(typeof api.trips.onChange, "function");
  assert.equal(api.trips.onHeartbeat, undefined);

  let received;
  const unsubscribe = api.receiver.onHeartbeat((heartbeat) => { received = heartbeat; });
  listeners.get("receiver:heartbeat")(null, { device_id: "RL-TEST" });
  assert.equal(received.device_id, "RL-TEST");
  unsubscribe();
  assert.equal(listeners.has("receiver:heartbeat"), false);
});
