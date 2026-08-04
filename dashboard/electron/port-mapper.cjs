const dgram = require("node:dgram");
const http = require("node:http");
const https = require("node:https");
const { execFile } = require("node:child_process");
const { EventEmitter } = require("node:events");

const SSDP_ADDRESS = "239.255.255.250";
const SSDP_PORT = 1900;

function runFile(command, args) {
  return new Promise((resolve, reject) => {
    execFile(command, args, { windowsHide: true, timeout: 8000 }, (error, stdout) => {
      if (error) reject(error);
      else resolve(stdout);
    });
  });
}

async function defaultGateway() {
  if (process.platform !== "win32") return null;
  const output = await runFile("route.exe", ["print", "-4", "0.0.0.0"]);
  for (const line of output.split(/\r?\n/)) {
    const match = line.trim().match(/^0\.0\.0\.0\s+0\.0\.0\.0\s+(\d+\.\d+\.\d+\.\d+)\s+/);
    if (match) return match[1];
  }
  return null;
}

function isPublicIpv4(address) {
  const parts = String(address).split(".").map(Number);
  if (parts.length !== 4 || parts.some((part) => !Number.isInteger(part) || part < 0 || part > 255)) return false;
  const [a, b, c] = parts;
  if (a === 0 || a === 10 || a === 127 || a >= 224) return false;
  if (a === 100 && b >= 64 && b <= 127) return false;
  if (a === 169 && b === 254) return false;
  if (a === 172 && b >= 16 && b <= 31) return false;
  if (a === 192 && b === 168) return false;
  if (a === 192 && b === 0 && c === 0) return false;
  if (a === 192 && b === 0 && c === 2) return false;
  if (a === 192 && b === 88 && c === 99) return false;
  if (a === 198 && (b === 18 || b === 19)) return false;
  if (a === 198 && b === 51 && c === 100) return false;
  if (a === 203 && b === 0 && c === 113) return false;
  return true;
}

function udpRequest(host, port, payload, validator, attempts = 3) {
  return new Promise((resolve, reject) => {
    const socket = dgram.createSocket("udp4");
    let attempt = 0;
    let timer = null;
    let finished = false;

    const finish = (error, value) => {
      if (finished) return;
      finished = true;
      if (timer) clearTimeout(timer);
      socket.close();
      if (error) reject(error);
      else resolve(value);
    };

    const send = () => {
      attempt += 1;
      socket.send(payload, port, host, (error) => {
        if (error) finish(error);
      });
      timer = setTimeout(() => {
        if (attempt >= attempts) finish(new Error(`No response from ${host}:${port}`));
        else send();
      }, 550 * attempt);
    };

    socket.on("message", (message) => {
      try {
        const result = validator(message);
        if (result !== undefined) finish(null, result);
      } catch (error) {
        finish(error);
      }
    });
    socket.on("error", (error) => finish(error));
    socket.bind(0, () => send());
  });
}

function parseNatPmpResponse(message, expectedOpcode) {
  if (message.length < 8 || message[0] !== 0 || message[1] !== expectedOpcode + 128) return undefined;
  const resultCode = message.readUInt16BE(2);
  if (resultCode !== 0) throw new Error(`NAT-PMP gateway returned error ${resultCode}`);
  return message;
}

async function natPmpMap(internalPort, suggestedExternalPort, lifetimeSeconds) {
  const gateway = await defaultGateway();
  if (!gateway) throw new Error("Default IPv4 gateway was not found");

  const publicResponse = await udpRequest(
    gateway,
    5351,
    Buffer.from([0, 0]),
    (message) => parseNatPmpResponse(message, 0),
  );
  if (publicResponse.length < 12) throw new Error("NAT-PMP public-address response was incomplete");
  const publicIp = Array.from(publicResponse.subarray(8, 12)).join(".");

  const request = Buffer.alloc(12);
  request[0] = 0;
  request[1] = 2;
  request.writeUInt16BE(internalPort, 4);
  request.writeUInt16BE(suggestedExternalPort, 6);
  request.writeUInt32BE(lifetimeSeconds, 8);
  const mapResponse = await udpRequest(
    gateway,
    5351,
    request,
    (message) => parseNatPmpResponse(message, 2),
  );
  if (mapResponse.length < 16) throw new Error("NAT-PMP mapping response was incomplete");

  return {
    method: "NAT-PMP",
    publicIp,
    publicPort: mapResponse.readUInt16BE(10),
    lifetimeSeconds: mapResponse.readUInt32BE(12),
    remove: () => natPmpUnmap(gateway, internalPort, mapResponse.readUInt16BE(10)),
  };
}

async function natPmpUnmap(gateway, internalPort, externalPort) {
  const request = Buffer.alloc(12);
  request[0] = 0;
  request[1] = 2;
  request.writeUInt16BE(internalPort, 4);
  request.writeUInt16BE(externalPort, 6);
  request.writeUInt32BE(0, 8);
  try {
    await udpRequest(gateway, 5351, request, (message) => parseNatPmpResponse(message, 2), 1);
  } catch {
    // Lease expiry remains a safe fallback if the gateway is no longer reachable.
  }
}

function discoverUpnpLocations(timeoutMs = 2300) {
  return new Promise((resolve, reject) => {
    const socket = dgram.createSocket("udp4");
    const locations = new Set();
    const query = Buffer.from([
      "M-SEARCH * HTTP/1.1",
      `HOST: ${SSDP_ADDRESS}:${SSDP_PORT}`,
      'MAN: "ssdp:discover"',
      "MX: 2",
      "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1",
      "",
      "",
    ].join("\r\n"));

    const timer = setTimeout(() => {
      socket.close();
      resolve([...locations]);
    }, timeoutMs);
    socket.on("message", (message) => {
      const match = message.toString("utf8").match(/^location:\s*(.+)$/im);
      if (match) locations.add(match[1].trim());
    });
    socket.on("error", (error) => {
      clearTimeout(timer);
      socket.close();
      reject(error);
    });
    socket.bind(0, () => {
      socket.setBroadcast(true);
      socket.send(query, SSDP_PORT, SSDP_ADDRESS);
      setTimeout(() => socket.send(query, SSDP_PORT, SSDP_ADDRESS), 450);
    });
  });
}

function requestText(url, options = {}, body = null) {
  return new Promise((resolve, reject) => {
    const parsed = new URL(url);
    const transport = parsed.protocol === "https:" ? https : http;
    const request = transport.request(parsed, options, (response) => {
      const chunks = [];
      response.on("data", (chunk) => chunks.push(chunk));
      response.on("end", () => {
        const text = Buffer.concat(chunks).toString("utf8");
        if ((response.statusCode ?? 500) >= 400) reject(new Error(`Router HTTP ${response.statusCode}: ${text.slice(0, 160)}`));
        else resolve(text);
      });
    });
    request.setTimeout(5000, () => request.destroy(new Error("Router request timed out")));
    request.on("error", reject);
    if (body) request.write(body);
    request.end();
  });
}

function decodeXml(value) {
  return value.replaceAll("&amp;", "&").replaceAll("&lt;", "<").replaceAll("&gt;", ">").replaceAll("&quot;", '"');
}

async function findUpnpControl() {
  const locations = await discoverUpnpLocations();
  if (!locations.length) throw new Error("No UPnP Internet Gateway Device answered discovery");
  for (const location of locations) {
    try {
      const description = await requestText(location);
      const services = description.match(/<service>[^]*?<\/service>/gi) ?? [];
      for (const service of services) {
        const type = service.match(/<serviceType>([^<]+)<\/serviceType>/i)?.[1];
        const controlPath = service.match(/<controlURL>([^<]+)<\/controlURL>/i)?.[1];
        if (type && controlPath && /WAN(?:IP|PPP)Connection:/i.test(type)) {
          return { serviceType: decodeXml(type.trim()), controlUrl: new URL(decodeXml(controlPath.trim()), location).href };
        }
      }
    } catch {
      // Try another SSDP response; routers often advertise several descriptions.
    }
  }
  throw new Error("UPnP gateway did not advertise a WAN connection service");
}

async function soap(control, action, fields = {}) {
  const bodyFields = Object.entries(fields).map(([name, value]) => `<${name}>${value}</${name}>`).join("");
  const body = `<?xml version="1.0"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/"><s:Body><u:${action} xmlns:u="${control.serviceType}">${bodyFields}</u:${action}></s:Body></s:Envelope>`;
  return requestText(control.controlUrl, {
    method: "POST",
    headers: {
      "Content-Type": 'text/xml; charset="utf-8"',
      "SOAPAction": `"${control.serviceType}#${action}"`,
      "Content-Length": Buffer.byteLength(body),
    },
  }, body);
}

async function upnpMap(internalPort, localIp, suggestedExternalPort, lifetimeSeconds) {
  const control = await findUpnpControl();
  const publicResponse = await soap(control, "GetExternalIPAddress");
  const publicIp = publicResponse.match(/<NewExternalIPAddress>([^<]+)<\/NewExternalIPAddress>/i)?.[1]?.trim();
  if (!publicIp) throw new Error("UPnP gateway did not return an external IP address");

  let publicPort = suggestedExternalPort;
  let lastError = null;
  for (let attempt = 0; attempt < 8; attempt += 1) {
    try {
      await soap(control, "AddPortMapping", {
        NewRemoteHost: "",
        NewExternalPort: publicPort,
        NewProtocol: "TCP",
        NewInternalPort: internalPort,
        NewInternalClient: localIp,
        NewEnabled: 1,
        NewPortMappingDescription: "RoadLink Fleet telemetry",
        NewLeaseDuration: lifetimeSeconds,
      });
      return {
        method: "UPnP",
        publicIp,
        publicPort,
        lifetimeSeconds,
        remove: async () => {
          try {
            await soap(control, "DeletePortMapping", { NewRemoteHost: "", NewExternalPort: publicPort, NewProtocol: "TCP" });
          } catch {
            // Lease expiry remains a safe fallback.
          }
        },
      };
    } catch (error) {
      lastError = error;
      publicPort = 40000 + Math.floor(Math.random() * 20001);
    }
  }
  throw lastError ?? new Error("UPnP gateway refused the port mapping");
}

class AutoPortMapper extends EventEmitter {
  constructor() {
    super();
    this.mapping = null;
    this.renewTimer = null;
    this.running = false;
  }

  async start(internalPort, localIp) {
    await this.stop();
    this.running = true;
    const suggestedPort = 40000 + Math.floor(Math.random() * 20001);
    const errors = [];
    for (const create of [
      () => natPmpMap(internalPort, suggestedPort, 3600),
      () => upnpMap(internalPort, localIp, suggestedPort, 3600),
    ]) {
      try {
        const mapping = await create();
        if (!this.running) {
          await mapping.remove();
          return null;
        }
        if (!isPublicIpv4(mapping.publicIp)) {
          await mapping.remove();
          throw new Error(`${mapping.method} returned ${mapping.publicIp}; the router is probably behind CGNAT or double NAT`);
        }
        this.mapping = { ...mapping, internalPort, localIp };
        this.emit("mapped", this.publicState());
        const renewAfter = Math.max(60, Math.floor(mapping.lifetimeSeconds / 2)) * 1000;
        this.renewTimer = setTimeout(() => {
          if (!this.running) return;
          this.start(internalPort, localIp).catch((error) => this.emit("error", error));
        }, renewAfter);
        return this.publicState();
      } catch (error) {
        errors.push(error instanceof Error ? error.message : String(error));
      }
    }
    throw new Error(`Automatic router mapping failed. NAT-PMP: ${errors[0] ?? "unavailable"}. UPnP: ${errors[1] ?? "unavailable"}.`);
  }

  publicState() {
    if (!this.mapping) return null;
    return {
      method: this.mapping.method,
      publicIp: this.mapping.publicIp,
      publicPort: this.mapping.publicPort,
      lifetimeSeconds: this.mapping.lifetimeSeconds,
    };
  }

  async stop() {
    this.running = false;
    if (this.renewTimer) clearTimeout(this.renewTimer);
    this.renewTimer = null;
    const previous = this.mapping;
    this.mapping = null;
    if (previous?.remove) await previous.remove();
  }
}

module.exports = { AutoPortMapper, defaultGateway, isPublicIpv4 };
