const { app, BrowserWindow, ipcMain, shell } = require("electron");
const { spawn } = require("node:child_process");
const path = require("node:path");
const { TelemetryReceiver } = require("./telemetry-server.cjs");

const isDevelopment = Boolean(process.env.ELECTRON_START_URL);
let mainWindow = null;
let receiver = null;
let quitting = false;

app.setAppUserModelId("dev.roadlink.fleet");
// v0.1.0 could leave an invisible elevated process locking Electron's default
// profile. Use a stable RoadLink-owned profile name that is independent of the
// executable product-name folder and will remain unchanged in later versions.
app.setPath("userData", path.join(app.getPath("appData"), "RoadLink Fleet Data"));

function createWindow() {
  const window = new BrowserWindow({
    width: 1540,
    height: 960,
    minWidth: 1120,
    minHeight: 720,
    backgroundColor: "#0a0e12",
    show: true,
    autoHideMenuBar: true,
    title: "RoadLink Fleet",
    webPreferences: {
      preload: path.join(__dirname, "preload.cjs"),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });

  mainWindow = window;

  window.on("closed", () => {
    if (mainWindow === window) mainWindow = null;
  });

  window.webContents.on(
    "did-fail-load",
    (_event, errorCode, errorDescription, validatedURL) => {
      console.error("RoadLink Fleet failed to load", {
        errorCode,
        errorDescription,
        validatedURL,
      });
    },
  );

  window.webContents.setWindowOpenHandler(({ url }) => {
    if (url.startsWith("https://")) shell.openExternal(url);
    return { action: "deny" };
  });

  if (isDevelopment) {
    window.loadURL(process.env.ELECTRON_START_URL);
  } else {
    window.loadFile(path.join(__dirname, "..", "dist", "index.html"));
  }

  // Do not make first paint a precondition for showing the native window.
  // The map and network fonts/tiles may be unavailable during startup.
  window.center();
  window.show();
  window.focus();
}

function sendToRenderer(channel, payload) {
  if (mainWindow && !mainWindow.isDestroyed()) mainWindow.webContents.send(channel, payload);
}

function installFirewallRule(port) {
  if (process.platform !== "win32") return Promise.reject(new Error("Firewall setup is only available on Windows"));
  const safePort = Number(port);
  if (!Number.isInteger(safePort) || safePort < 1 || safePort > 65535) return Promise.reject(new Error("Invalid TCP port"));
  const ruleName = `RoadLink Fleet Telemetry ${safePort}`;
  const command = [
    `$argsList = @('advfirewall','firewall','add','rule','name=${ruleName}','dir=in','action=allow','protocol=TCP','localport=${safePort}','profile=private')`,
    "$result = Start-Process -FilePath 'netsh.exe' -ArgumentList $argsList -Verb RunAs -Wait -PassThru",
    "exit $result.ExitCode",
  ].join("; ");
  return new Promise((resolve, reject) => {
    const child = spawn("powershell.exe", ["-NoProfile", "-NonInteractive", "-WindowStyle", "Hidden", "-Command", command], {
      windowsHide: true,
      stdio: "ignore",
    });
    child.on("error", reject);
    child.on("exit", (code) => {
      if (code === 0) resolve({ ok: true, ruleName });
      else reject(new Error(code === 1223 ? "Administrator approval was cancelled" : `Windows Firewall returned exit code ${code}`));
    });
  });
}

function registerReceiverIpc() {
  ipcMain.handle("receiver:get-state", () => receiver.state());
  ipcMain.handle("receiver:start", () => receiver.start());
  ipcMain.handle("receiver:stop", () => receiver.stop());
  ipcMain.handle("receiver:rotate-key", () => receiver.rotateKey());
  ipcMain.handle("receiver:update", (_event, patch) => {
    const safePatch = {};
    if (Object.hasOwn(patch ?? {}, "port")) safePatch.port = Number(patch.port);
    if (Object.hasOwn(patch ?? {}, "accessKey")) safePatch.accessKey = String(patch.accessKey);
    if (Object.hasOwn(patch ?? {}, "autoPortMap")) safePatch.autoPortMap = patch.autoPortMap === true;
    if (Object.hasOwn(patch ?? {}, "enabled")) safePatch.enabled = patch.enabled === true;
    return receiver.updateConfig(safePatch);
  });
  ipcMain.handle("receiver:add-firewall-rule", () => installFirewallRule(receiver.state().port));
  ipcMain.handle("receiver:open-log-folder", () => shell.showItemInFolder(receiver.state().logPath));
}

const hasSingleInstanceLock = app.requestSingleInstanceLock();

if (!hasSingleInstanceLock) {
  app.quit();
} else {
  app.on("second-instance", () => {
    if (!mainWindow) {
      createWindow();
      return;
    }
    if (mainWindow.isMinimized()) mainWindow.restore();
    mainWindow.show();
    mainWindow.focus();
  });

  app.whenReady().then(async () => {
    receiver = new TelemetryReceiver({ dataDirectory: app.getPath("userData") });
    receiver.on("state", (state) => sendToRenderer("receiver:state", state));
    receiver.on("telemetry", (event) => sendToRenderer("receiver:telemetry", event));
    registerReceiverIpc();
    createWindow();
    if (receiver.config.enabled) {
      try {
        await receiver.start();
      } catch (error) {
        console.error("RoadLink receiver failed to start", error);
      }
    }
    app.on("activate", () => {
      if (BrowserWindow.getAllWindows().length === 0) createWindow();
    });
  });
}

app.on("before-quit", (event) => {
  if (quitting || !receiver) return;
  event.preventDefault();
  quitting = true;
  receiver.stop({ disable: false }).finally(() => app.quit());
});

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") app.quit();
});
