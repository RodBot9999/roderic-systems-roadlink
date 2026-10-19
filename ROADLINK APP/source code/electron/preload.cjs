const { clipboard, contextBridge, ipcRenderer } = require("electron");

contextBridge.exposeInMainWorld("roadlinkDesktop", {
  platform: process.platform,
  version: ipcRenderer.sendSync("app:get-version"),
  copyText: (value) => clipboard.writeText(String(value)),
  receiver: {
    getState: () => ipcRenderer.invoke("receiver:get-state"),
    configureStreaming: (id, patch, revision) => ipcRenderer.invoke("receiver:configure-streaming", id, patch, revision),
    start: () => ipcRenderer.invoke("receiver:start"),
    stop: () => ipcRenderer.invoke("receiver:stop"),
    update: (patch) => ipcRenderer.invoke("receiver:update", patch),
    rotateKey: () => ipcRenderer.invoke("receiver:rotate-key"),
    addFirewallRule: () => ipcRenderer.invoke("receiver:add-firewall-rule"),
    openLogFolder: () => ipcRenderer.invoke("receiver:open-log-folder"),
    onState: (callback) => {
      const listener = (_event, state) => callback(state);
      ipcRenderer.on("receiver:state", listener);
      return () => ipcRenderer.removeListener("receiver:state", listener);
    },
    onTelemetry: (callback) => {
      const listener = (_event, telemetry) => callback(telemetry);
      ipcRenderer.on("receiver:telemetry", listener);
      return () => ipcRenderer.removeListener("receiver:telemetry", listener);
    },
    onHeartbeat: (callback) => {
      const listener = (_event, heartbeat) => callback(heartbeat);
      ipcRenderer.on("receiver:heartbeat", listener);
      return () => ipcRenderer.removeListener("receiver:heartbeat", listener);
    },
  },
  trips: {
    list: () => ipcRenderer.invoke("trips:list"),
    active: () => ipcRenderer.invoke("trips:active"),
    start: (details) => ipcRenderer.invoke("trips:start", details),
    stop: () => ipcRenderer.invoke("trips:stop"),
    get: (id) => ipcRenderer.invoke("trips:get", id),
    openFolder: () => ipcRenderer.invoke("trips:open-folder"),
    onChange: (callback) => {
      const listener = (_event, trip) => callback(trip);
      ipcRenderer.on("trips:change", listener);
      return () => ipcRenderer.removeListener("trips:change", listener);
    },
    onError: (callback) => {
      const listener = (_event, message) => callback(message);
      ipcRenderer.on("trips:error", listener);
      return () => ipcRenderer.removeListener("trips:error", listener);
    },
  },
});
