const { clipboard, contextBridge, ipcRenderer } = require("electron");

contextBridge.exposeInMainWorld("roadlinkDesktop", {
  platform: process.platform,
  version: "0.2.0",
  copyText: (value) => clipboard.writeText(String(value)),
  receiver: {
    getState: () => ipcRenderer.invoke("receiver:get-state"),
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
  },
});
