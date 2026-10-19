type GpsReport = { valid?: boolean; latitude?: number | null; longitude?: number | null };
type GpsEvent = { type?: string; gps?: GpsReport };
type PreviousPosition = { hasGpsFix?: boolean; latitude: number; longitude: number };

export function liveGpsPosition(event: GpsEvent, previous?: PreviousPosition) {
  const gps = event.gps;
  const valid = gps?.valid === true && typeof gps.latitude === "number" && Number.isFinite(gps.latitude)
    && typeof gps.longitude === "number" && Number.isFinite(gps.longitude)
    && Math.abs(gps.latitude) <= 90 && Math.abs(gps.longitude) <= 180
    && !(gps.latitude === 0 && gps.longitude === 0);
  // Heartbeats have no GPS section: keep the last fix without reading absent coordinates.
  const hasGpsFix = gps ? valid : event.type === "heartbeat" ? previous?.hasGpsFix ?? false : false;
  return {
    hasGpsFix,
    latitude: valid ? gps.latitude as number : previous?.latitude ?? 0,
    longitude: valid ? gps.longitude as number : previous?.longitude ?? 0,
  };
}
