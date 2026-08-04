const crypto = require("node:crypto");
const fs = require("node:fs");
const http = require("node:http");
const os = require("node:os");
const path = require("node:path");
const { EventEmitter } = require("node:events");
const { AutoPortMapper } = require("./port-mapper.cjs");

const MAX_BODY_BYTES = 64 * 1024;
const DEFAULT_PORT = 8080;

function generateKey() {
  return String(crypto.randomInt(0, 1_000_000)).padStart(6, "0");
}

function lanAddresses() {
  const addresses = [];
  for (const [name, entries] of Object.entries(os.networkInterfaces())) {
    for (const entry of entries ?? []) {
      if (entry.family === "IPv4" && !entry.internal) addresses.push({ name, address: entry.address });
    }
  }
  return addresses;
}

function safeKeyEqual(candidate, expected) {
  const left = Buffer.from(String(candidate ?? ""));
  const right = Buffer.from(String(expected));
  return left.length === right.length && crypto.timingSafeEqual(left, right);
}

function readJson(filePath, fallback) {
  try {
    return { ...fallback, ...JSON.parse(fs.readFileSync(filePath, "utf8")) };
  } catch {
    return fallback;
  }
}

class TelemetryReceiver extends EventEmitter {
  constructor({ dataDirectory }) {
    super();
    this.dataDirectory = dataDirectory;
    this.configPath = path.join(dataDirectory, "receiver-config.json");
    this.logDirectory = path.join(dataDirectory, "telemetry");
    this.logPath = path.join(this.logDirectory, "roadlink-telemetry.jsonl");
    fs.mkdirSync(this.logDirectory, { recursive: true });
    this.config = this.validateConfig(readJson(this.configPath, {
      enabled: true,
      port: DEFAULT_PORT,
      accessKey: generateKey(),
      autoPortMap: false,
    }));
    this.server = null;
    this.mapper = new AutoPortMapper();
    this.mapping = null;
    this.lastError = null;
    this.packetCount = 0;
    this.lastPacketAt = null;
    this.authFailures = new Map();
    this.mapper.on("mapped", (mapping) => {
      this.mapping = mapping;
      this.lastError = null;
      this.emitState();
    });
    this.mapper.on("error", (error) => {
      this.mapping = null;
      this.lastError = error.message;
      this.emitState();
    });
    this.saveConfig();
  }

  validateConfig(candidate) {
    const port = Number(candidate.port);
    const accessKey = String(candidate.accessKey ?? "");
    return {
      enabled: candidate.enabled !== false,
      port: Number.isInteger(port) && port >= 1 && port <= 65535 ? port : DEFAULT_PORT,
      accessKey: /^\d{6}$/.test(accessKey) ? accessKey : generateKey(),
      autoPortMap: candidate.autoPortMap === true,
    };
  }

  saveConfig() {
    fs.mkdirSync(this.dataDirectory, { recursive: true });
    const temporary = `${this.configPath}.tmp`;
    fs.writeFileSync(temporary, `${JSON.stringify(this.config, null, 2)}\n`, "utf8");
    fs.renameSync(temporary, this.configPath);
  }

  state() {
    const addresses = lanAddresses();
    const primaryAddress = addresses[0]?.address ?? "127.0.0.1";
    return {
      running: Boolean(this.server?.listening),
      status: this.server?.listening ? "listening" : this.config.enabled ? "stopped" : "disabled",
      port: this.config.port,
      accessKey: this.config.accessKey,
      autoPortMap: this.config.autoPortMap,
      lanAddresses: addresses,
      localEndpoint: `http://${primaryAddress}:${this.config.port}/telemetry`,
      loopbackEndpoint: `http://127.0.0.1:${this.config.port}/telemetry`,
      publicEndpoint: this.mapping ? `http://${this.mapping.publicIp}:${this.mapping.publicPort}/telemetry` : null,
      mapping: this.mapping,
      packetCount: this.packetCount,
      lastPacketAt: this.lastPacketAt,
      lastError: this.lastError,
      logPath: this.logPath,
    };
  }

  emitState() {
    this.emit("state", this.state());
  }

  async start() {
    if (this.server?.listening) return this.state();
    this.config.enabled = true;
    this.saveConfig();
    this.lastError = null;
    this.server = http.createServer((request, response) => this.handleRequest(request, response));
    this.server.requestTimeout = 15000;
    this.server.headersTimeout = 10000;
    this.server.keepAliveTimeout = 3000;

    await new Promise((resolve, reject) => {
      const onError = (error) => {
        this.server?.removeListener("listening", onListening);
        reject(error);
      };
      const onListening = () => {
        this.server?.removeListener("error", onError);
        resolve();
      };
      this.server.once("error", onError);
      this.server.once("listening", onListening);
      this.server.listen(this.config.port, "0.0.0.0");
    }).catch((error) => {
      this.server = null;
      this.lastError = `Could not listen on port ${this.config.port}: ${error.message}`;
      this.emitState();
      throw error;
    });

    this.emitState();
    if (this.config.autoPortMap) this.startMapping();
    return this.state();
  }

  async stop({ disable = true } = {}) {
    await this.mapper.stop();
    this.mapping = null;
    const server = this.server;
    this.server = null;
    if (server) await new Promise((resolve) => server.close(() => resolve()));
    if (disable) {
      this.config.enabled = false;
      this.saveConfig();
    }
    this.emitState();
    return this.state();
  }

  async restart() {
    await this.stop({ disable: false });
    return this.start();
  }

  async updateConfig(patch) {
    const previous = this.config;
    this.config = this.validateConfig({ ...this.config, ...patch });
    this.saveConfig();
    const requiresRestart = previous.port !== this.config.port || previous.enabled !== this.config.enabled;
    if (!this.config.enabled) return this.stop({ disable: true });
    if (requiresRestart || !this.server?.listening) return this.restart();
    if (previous.autoPortMap !== this.config.autoPortMap) {
      if (this.config.autoPortMap) this.startMapping();
      else {
        await this.mapper.stop();
        this.mapping = null;
        this.emitState();
      }
    } else {
      this.emitState();
    }
    return this.state();
  }

  async rotateKey() {
    this.config.accessKey = generateKey();
    this.saveConfig();
    this.emitState();
    return this.state();
  }

  async startMapping() {
    if (!this.server?.listening) return;
    const localIp = lanAddresses()[0]?.address;
    if (!localIp) {
      this.lastError = "No LAN IPv4 address is available for router mapping";
      this.emitState();
      return;
    }
    this.mapping = null;
    this.lastError = null;
    this.emitState();
    try {
      this.mapping = await this.mapper.start(this.config.port, localIp);
      this.lastError = null;
    } catch (error) {
      this.mapping = null;
      this.lastError = error.message;
    }
    this.emitState();
  }

  isBlocked(address) {
    const record = this.authFailures.get(address);
    if (!record) return false;
    if (record.blockedUntil > Date.now()) return true;
    if (Date.now() - record.windowStarted > 60_000) this.authFailures.delete(address);
    return false;
  }

  recordAuthFailure(address) {
    const now = Date.now();
    const current = this.authFailures.get(address);
    const record = !current || now - current.windowStarted > 60_000
      ? { count: 1, windowStarted: now, blockedUntil: 0 }
      : { ...current, count: current.count + 1 };
    if (record.count >= 8) record.blockedUntil = now + 5 * 60_000;
    this.authFailures.set(address, record);
  }

  sendJson(response, status, payload) {
    const body = Buffer.from(JSON.stringify(payload));
    response.writeHead(status, {
      "Content-Type": "application/json; charset=utf-8",
      "Content-Length": body.length,
      "Cache-Control": "no-store",
      "X-Content-Type-Options": "nosniff",
    });
    response.end(body);
  }

  handleRequest(request, response) {
    const requestUrl = new URL(request.url ?? "/", "http://localhost");
    if (request.method === "GET" && (requestUrl.pathname === "/" || requestUrl.pathname === "/health")) {
      this.sendJson(response, 200, { ok: true, service: "roadlink-monitor" });
      return;
    }
    if (request.method !== "POST" || requestUrl.pathname !== "/telemetry") {
      this.sendJson(response, 404, { ok: false, error: "not_found" });
      return;
    }

    const client = request.socket.remoteAddress?.replace(/^::ffff:/, "") ?? "unknown";
    if (this.isBlocked(client)) {
      this.sendJson(response, 429, { ok: false, error: "too_many_auth_failures" });
      request.resume();
      return;
    }

    const chunks = [];
    let length = 0;
    let rejected = false;
    request.on("data", (chunk) => {
      if (rejected) return;
      length += chunk.length;
      if (length > MAX_BODY_BYTES) {
        rejected = true;
        this.sendJson(response, 413, { ok: false, error: "payload_too_large" });
        request.destroy();
      } else {
        chunks.push(chunk);
      }
    });
    request.on("end", () => {
      if (rejected) return;
      let payload;
      try {
        payload = JSON.parse(Buffer.concat(chunks).toString("utf8"));
        if (!payload || Array.isArray(payload) || typeof payload !== "object") throw new Error("JSON object required");
      } catch {
        this.sendJson(response, 400, { ok: false, error: "invalid_json" });
        return;
      }
      if (!safeKeyEqual(payload.access_key, this.config.accessKey)) {
        this.recordAuthFailure(client);
        this.sendJson(response, 401, { ok: false, error: "invalid_access_key" });
        return;
      }

      const { access_key: _secret, ...safePayload } = payload;
      const event = {
        ...safePayload,
        _received_at: new Date().toISOString(),
        _client: client,
      };
      try {
        fs.appendFileSync(this.logPath, `${JSON.stringify(event)}\n`, "utf8");
      } catch (error) {
        this.lastError = `Telemetry was accepted but could not be logged: ${error.message}`;
      }
      this.packetCount += 1;
      this.lastPacketAt = event._received_at;
      this.emit("telemetry", event);
      this.emitState();
      this.sendJson(response, 200, { ok: true });
    });
    request.on("error", () => {
      if (!response.headersSent) this.sendJson(response, 400, { ok: false, error: "request_error" });
    });
  }
}

module.exports = { TelemetryReceiver, generateKey, lanAddresses, safeKeyEqual };
