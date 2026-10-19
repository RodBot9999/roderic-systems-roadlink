// Only this small, known schema may cross from an HTTP request into app state/IPC.
const textFields = { device: 128, device_id: 128, imei: 32, firmware: 128, captured_at: 64 };
const gpsNumbers = {
  latitude: [-90, 90], longitude: [-180, 180], altitude_m: [-1000, 100000],
  speed_kmh: [0, 600], course_deg: [0, 360], heading_deg: [0, 360], satellites: [0, 100],
};
const obdNumbers = {
  rpm: [0, 30000], speed_kmh: [0, 600], coolant_c: [-100, 300],
  throttle_pct: [0, 100], map_kpa: [0, 1000], intake_c: [-100, 200],
  timing_deg: [-180, 180], voltage_v: [0, 100], fuel_pct: [0, 100],
  odometer_km: [0, 100000000], age_ms: [0, 4294967295],
};
const modemNumbers = { signal_bars: [0, 5], rssi: [-200, 200] };

function record(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function copyText(source, target, key, maxLength) {
  if (source[key] === undefined || source[key] === null) return;
  if (typeof source[key] !== "string" || source[key].length > maxLength) throw new Error(`Invalid ${key}`);
  target[key] = source[key];
}

function copyNumbers(source, target, limits) {
  for (const [key, [minimum, maximum]] of Object.entries(limits)) {
    if (source[key] === undefined) continue;
    const value = source[key];
    // A defective sensor value is missing data, never a renderer value.
    target[key] = typeof value === "number" && Number.isFinite(value) && value >= minimum && value <= maximum ? value : null;
  }
}

function sanitizeTelemetryPayload(payload, config) {
  if (!record(payload)) throw new Error("JSON object required");
  const safe = {};
  for (const [key, maxLength] of Object.entries(textFields)) copyText(payload, safe, key, maxLength);
  if (![safe.device_id, safe.imei, safe.device].some((value) => typeof value === "string" && value.trim())) {
    throw new Error("Device identity required");
  }
  for (const key of ["sequence", "uptime_ms"]) {
    if (payload[key] === undefined) continue;
    const value = payload[key];
    safe[key] = Number.isSafeInteger(value) && value >= 0 ? value : null;
  }
  if (config) safe.config = config;

  for (const [section, limits] of [["gps", gpsNumbers], ["obd", obdNumbers], ["modem", modemNumbers]]) {
    if (payload[section] === undefined || payload[section] === null) continue;
    if (!record(payload[section])) throw new Error(`Invalid ${section}`);
    const source = payload[section];
    const result = {};
    copyNumbers(source, result, limits);
    if (section === "gps") {
      if (source.valid !== undefined && typeof source.valid !== "boolean") throw new Error("Invalid gps.valid");
      result.valid = source.valid === true && typeof result.latitude === "number" && typeof result.longitude === "number"
        && !(result.latitude === 0 && result.longitude === 0);
      for (const key of ["utc_time", "utc_date"]) copyText(source, result, key, 32);
    }
    if (section === "modem") copyText(source, result, "technology", 48);
    safe[section] = result;
  }
  return safe;
}

module.exports = { sanitizeTelemetryPayload };
