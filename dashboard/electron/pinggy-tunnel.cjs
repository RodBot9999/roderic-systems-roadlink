const dns = require("node:dns");
const fs = require("node:fs");
const path = require("node:path");
const { execFile, spawn } = require("node:child_process");
const { EventEmitter } = require("node:events");
const { promisify } = require("node:util");

const FREE_TUNNEL_LIFETIME_MS = 60 * 60 * 1000;
const execFileAsync = promisify(execFile);

function sshCommand() {
  const windowsSsh = "C:\\Windows\\System32\\OpenSSH\\ssh.exe";
  return process.platform === "win32" && fs.existsSync(windowsSsh) ? windowsSsh : "ssh";
}

function sshKeygenCommand() {
  const windowsKeygen = "C:\\Windows\\System32\\OpenSSH\\ssh-keygen.exe";
  return process.platform === "win32" && fs.existsSync(windowsKeygen) ? windowsKeygen : "ssh-keygen";
}

function pinggyArguments(localPort, identityFile = null) {
  const args = [
    "-p", "443",
    "-o", "BatchMode=yes",
    "-o", "ExitOnForwardFailure=yes",
    "-o", "ServerAliveInterval=30",
    "-o", "ServerAliveCountMax=3",
    "-o", "StrictHostKeyChecking=accept-new",
  ];
  if (identityFile) args.push("-o", "IdentitiesOnly=yes", "-i", identityFile);
  args.push("-R", `0:127.0.0.1:${localPort}`, "tcp@free.pinggy.io");
  return args;
}

function outputHint(output) {
  const lines = output
    .replace(/\x1b\[[0-9;]*m/g, "")
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter(Boolean);
  return (lines.at(-1) ?? "").slice(0, 320);
}

class PinggyTunnel extends EventEmitter {
  constructor({ spawnProcess = spawn, lookup = dns.promises.lookup, command = sshCommand(),
    identityFile = null, createIdentity = null, keygenCommand = sshKeygenCommand() } = {}) {
    super();
    this.spawnProcess = spawnProcess;
    this.lookup = lookup;
    this.command = command;
    this.identityFile = identityFile;
    this.createIdentity = createIdentity;
    this.keygenCommand = keygenCommand;
    this.child = null;
    this.expiryTimer = null;
    this.startTimer = null;
    this.generation = 0;
    this.output = "";
    this.tunnelState = { status: "stopped", publicHost: null, publicIp: null,
      publicPort: null, startedAt: null, expiresAt: null, lastError: null };
  }

  state() { return { ...this.tunnelState }; }

  emitState() { this.emit("state", this.state()); }

  async ensureIdentity() {
    if (!this.identityFile || fs.existsSync(this.identityFile)) return;
    fs.mkdirSync(path.dirname(this.identityFile), { recursive: true });
    if (this.createIdentity) await this.createIdentity(this.identityFile);
    else {
      await execFileAsync(this.keygenCommand,
        ["-q", "-t", "ed25519", "-N", "", "-f", this.identityFile],
        { windowsHide: true });
    }
    if (!fs.existsSync(this.identityFile)) throw new Error("SSH key generation completed without creating a private key");
  }

  async start(localPort) {
    if (!Number.isInteger(localPort) || localPort < 1 || localPort > 65535) throw new Error("Invalid receiver port");
    await this.stop();
    const generation = ++this.generation;
    this.output = "";
    this.tunnelState = { status: "starting", publicHost: null, publicIp: null,
      publicPort: null, startedAt: null, expiresAt: null, lastError: null };
    this.emitState();

    try {
      await this.ensureIdentity();
    } catch (error) {
      if (generation !== this.generation) return this.state();
      const message = `Could not create the RoadLink Pinggy SSH identity: ${error.message}`;
      this.tunnelState = { status: "error", publicHost: null, publicIp: null,
        publicPort: null, startedAt: null, expiresAt: null, lastError: message };
      this.emitState();
      throw new Error(message);
    }
    if (generation !== this.generation) return this.state();

    return new Promise((resolve, reject) => {
      let settled = false;
      const fail = (error) => {
        if (generation !== this.generation) return;
        const message = error instanceof Error ? error.message : String(error);
        this.clearTimers();
        this.tunnelState = { status: "error", publicHost: null, publicIp: null,
          publicPort: null, startedAt: null, expiresAt: null, lastError: message };
        this.emitState();
        if (!settled) { settled = true; reject(new Error(message)); }
      };
      const activate = async (host, port) => {
        if (settled || generation !== this.generation) return;
        try {
          const resolved = await this.lookup(host, { family: 4 });
          if (generation !== this.generation) return;
          const now = Date.now();
          this.tunnelState = { status: "active", publicHost: host, publicIp: resolved.address,
            publicPort: port, startedAt: new Date(now).toISOString(),
            expiresAt: new Date(now + FREE_TUNNEL_LIFETIME_MS).toISOString(), lastError: null };
          clearTimeout(this.startTimer);
          this.startTimer = null;
          this.expiryTimer = setTimeout(() => this.stop("Free Pinggy tunnel reached its 60-minute limit"), FREE_TUNNEL_LIFETIME_MS);
          settled = true;
          this.emitState();
          resolve(this.state());
        } catch (error) {
          fail(new Error(`Pinggy address could not be resolved to IPv4: ${error.message}`));
          this.child?.kill();
        }
      };
      const consume = (data) => {
        this.output = (this.output + data.toString("utf8")).slice(-12000);
        const match = this.output.match(/tcp:\/\/([a-z0-9.-]+):(\d{1,5})/i);
        if (match) {
          const port = Number(match[2]);
          if (port >= 1 && port <= 65535) void activate(match[1], port);
        }
      };

      try {
        const child = this.spawnProcess(this.command, pinggyArguments(localPort, this.identityFile), {
          windowsHide: true,
          stdio: ["ignore", "pipe", "pipe"],
        });
        this.child = child;
        child.stdout?.on("data", consume);
        child.stderr?.on("data", consume);
        child.once("error", (error) => fail(new Error(`Could not start Windows SSH: ${error.message}`)));
        child.once("exit", (code) => {
          if (generation !== this.generation) return;
          this.child = null;
          if (!settled) {
            const hint = outputHint(this.output);
            fail(new Error(`Pinggy tunnel closed before providing an address (SSH exit ${code ?? "unknown"})${hint ? `: ${hint}` : ""}`));
          }
          else if (this.tunnelState.status === "error") return;
          else {
            this.clearTimers();
            this.tunnelState = { ...this.tunnelState, status: "error",
              lastError: "Pinggy tunnel disconnected. Start it again for a new address." };
            this.emitState();
          }
        });
        this.startTimer = setTimeout(() => {
          const hint = outputHint(this.output);
          fail(new Error(`Pinggy did not provide a TCP address within 30 seconds${hint ? `: ${hint}` : ""}`));
          child.kill();
        }, 30000);
      } catch (error) { fail(error); }
    });
  }

  clearTimers() {
    if (this.startTimer) clearTimeout(this.startTimer);
    if (this.expiryTimer) clearTimeout(this.expiryTimer);
    this.startTimer = null;
    this.expiryTimer = null;
  }

  async stop(reason = null) {
    this.generation += 1;
    this.clearTimers();
    const child = this.child;
    this.child = null;
    if (child && !child.killed) child.kill();
    this.tunnelState = { status: "stopped", publicHost: null, publicIp: null,
      publicPort: null, startedAt: null, expiresAt: null, lastError: reason };
    this.emitState();
    return this.state();
  }
}

module.exports = { PinggyTunnel, pinggyArguments, outputHint, FREE_TUNNEL_LIFETIME_MS };
