import { Activity, AlertTriangle, BarChart3, Car, Clock, Database, FolderOpen, Gauge, MapPin, Navigation, Pause, Play, Square, Thermometer } from "lucide-react";
import { useEffect, useMemo, useRef, useState } from "react";
import type { Map as LeafletMap } from "leaflet";

export type DeviceCard = { id: string; nickname: string; vehicle: string; plate: string; status: "moving" | "parked" | "offline"; updatedAt: number; hasGpsFix?: boolean; gpsEnabled?: boolean; obdEnabled?: boolean; obdWarning?: boolean; obdMissing?: string[]; streamRunning?: boolean; speed: number; color: string };
export type Health = { level: string; message: string };

export function DevicesGrid({ devices, selectedId, onSelect, health }: { devices: DeviceCard[]; selectedId: string; onSelect: (id: string) => void; health: (device: DeviceCard) => Health }) {
  return <section className="device-grid-panel panel">
    <div className="section-title"><div><p className="eyebrow">Device directory</p><h2>All RoadLinks</h2><span>Choose a unit to inspect its live readings and alerts.</span></div><Database size={22} /></div>
    <div className="device-grid">
      {devices.map((device) => { const state = health(device); return <button key={device.id} className={`device-grid-card health-${state.level} ${selectedId === device.id ? "is-selected" : ""}`} onClick={() => onSelect(device.id)}>
        <div className="device-grid-card-top"><span className="vehicle-avatar" style={{ "--device-color": device.color } as React.CSSProperties}><Car size={21} /></span><span className={`health-badge health-${state.level}`}>{state.level === "green" ? "Healthy" : state.level === "yellow" ? "Warning" : "Problem"}</span></div>
        <strong>{device.nickname}</strong><small>{device.id} · {device.plate}</small><span className="device-grid-vehicle">{device.vehicle}</span>
        <div className="device-grid-status">{state.level === "green" ? <Activity size={15} /> : <AlertTriangle size={15} />}<span>{state.message}</span></div>
        {device.status !== "offline" && device.gpsEnabled && !device.hasGpsFix && <span className="no-fix-label"><MapPin size={13} /> No GPS fix · location unavailable</span>}
      </button>; })}
      {!devices.length && <div className="empty-state"><Database size={24} /><strong>No devices yet</strong><span>Authenticated units will appear here.</span></div>}
    </div>
  </section>;
}

function TripRouteMap({ samples, cursor }: { samples: TripSample[]; cursor: number }) {
  const node = useRef<HTMLDivElement>(null);
  const map = useRef<LeafletMap | null>(null);
  const leaflet = useRef<typeof import("leaflet") | null>(null);
  const layer = useRef<import("leaflet").LayerGroup | null>(null);
  const [ready, setReady] = useState(false);
  const fittedSamples = useRef<TripSample[] | null>(null);
  useEffect(() => {
    let disposed = false;
    void import("leaflet").then((L) => {
      if (disposed || !node.current) return;
      leaflet.current = L;
      const instance = L.map(node.current, { zoomControl: false }).setView([20.6767, -103.3651], 12);
      L.tileLayer("https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png", { maxZoom: 20, attribution: "&copy; OpenStreetMap &copy; CARTO" }).addTo(instance);
      L.control.zoom({ position: "bottomright" }).addTo(instance);
      layer.current = L.layerGroup().addTo(instance);
      map.current = instance;
      setReady(true);
      window.setTimeout(() => instance.invalidateSize(), 80);
    });
    return () => { disposed = true; map.current?.remove(); map.current = null; layer.current = null; };
  }, []);
  useEffect(() => {
    const L = leaflet.current;
    if (!L || !layer.current || !map.current) return;
    layer.current.clearLayers();
    let segment: [number, number][] = [];
    const all: [number, number][] = [];
    const flush = () => { if (segment.length > 1) L.polyline(segment, { color: "#b8f34a", weight: 4 }).addTo(layer.current!); segment = []; };
    samples.forEach((sample) => {
      if (sample.latitude == null || sample.longitude == null) { flush(); return; }
      const point: [number, number] = [sample.latitude, sample.longitude];
      segment.push(point); all.push(point);
    });
    flush();
    const selected = samples[cursor];
    if (selected?.latitude != null && selected.longitude != null) L.circleMarker([selected.latitude, selected.longitude], { radius: 9, color: "#fff", weight: 3, fillColor: "#ffbd59", fillOpacity: 1 }).addTo(layer.current);
    if (all.length && fittedSamples.current !== samples) map.current.fitBounds(L.latLngBounds(all).pad(0.25), { maxZoom: 16, animate: false });
    fittedSamples.current = samples;
  }, [samples, cursor, ready]);
  return <div className="trip-map"><div ref={node} className="trip-map-canvas" />{!samples.some((item) => item.latitude != null) && <div className="trip-no-route"><MapPin size={24} /><strong>No GPS route recorded</strong><span>Telemetry is preserved, but no position is invented.</span></div>}</div>;
}

function TripGraph({ samples, cursor, field, onCursor }: { samples: TripSample[]; cursor: number; field: "speed" | "rpm" | "coolant"; onCursor: (index: number) => void }) {
  const values = samples.map((sample) => sample[field]);
  const max = Math.max(1, ...values.filter((value): value is number => value != null));
  let segment: string[] = [];
  const paths: string[] = [];
  values.forEach((value, index) => {
    if (value == null) { if (segment.length > 1) paths.push(segment.join(" ")); segment = []; return; }
    segment.push(`${segment.length ? "L" : "M"}${(index / Math.max(1, values.length - 1) * 1000).toFixed(1)},${(180 - value / max * 160).toFixed(1)}`);
  });
  if (segment.length > 1) paths.push(segment.join(" "));
  return <div className="trip-graph" role="img" aria-label={`${field} over trip time`}>
    <svg viewBox="0 0 1000 190" preserveAspectRatio="none"><path d="M0 180H1000 M0 100H1000 M0 20H1000" className="trip-grid-line" />{paths.map((path, index) => <path key={index} d={path} className="trip-plot-line" />)}<line x1={cursor / Math.max(1, samples.length - 1) * 1000} x2={cursor / Math.max(1, samples.length - 1) * 1000} y1="0" y2="190" className="trip-cursor" /></svg>
    <input type="range" min="0" max={Math.max(0, samples.length - 1)} value={cursor} onChange={(event) => onCursor(Number(event.target.value))} aria-label="Replay position" />
  </div>;
}

function formatTime(timestamp: number) { return new Date(timestamp).toLocaleString([], { dateStyle: "medium", timeStyle: "short" }); }
function duration(seconds: number) { return `${Math.floor(seconds / 3600)}h ${Math.floor(seconds % 3600 / 60)}m ${seconds % 60}s`; }

export function TripsPage({ devices, demoMode }: { devices: DeviceCard[]; demoMode: boolean }) {
  const [trips, setTrips] = useState<TripRecord[]>([]);
  const [active, setActive] = useState<ActiveTrip | null>(null);
  const [deviceId, setDeviceId] = useState("");
  const [selected, setSelected] = useState<(TripRecord & { samples: TripSample[] }) | null>(null);
  const [cursor, setCursor] = useState(0);
  const [metric, setMetric] = useState<"speed" | "rpm" | "coolant">("speed");
  const [playing, setPlaying] = useState(false);
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  const refresh = async () => { const service = window.roadlinkDesktop?.trips; if (!service) return; setTrips(await service.list()); setActive(await service.active()); };
  useEffect(() => {
    void refresh();
    const service = window.roadlinkDesktop?.trips;
    const offChange = service?.onChange((trip) => setActive(trip));
    const offError = service?.onError(setError);
    return () => { offChange?.(); offError?.(); };
  }, []);
  useEffect(() => { if (!deviceId && devices[0]) setDeviceId(devices[0].id); }, [devices, deviceId]);
  const chooseTrip = async (id: string) => { try { const trip = await window.roadlinkDesktop?.trips.get(id); if (trip) { setSelected(trip); setCursor(0); setPlaying(false); } } catch (reason) { setError(String(reason)); } };
  useEffect(() => {
    if (!playing || !selected?.samples.length) return;
    const timer = window.setInterval(() => setCursor((index) => {
      if (index >= selected.samples.length - 1) { setPlaying(false); return index; }
      return index + 1;
    }), 350);
    return () => window.clearInterval(timer);
  }, [playing, selected]);
  const start = async () => {
    const device = devices.find((item) => item.id === deviceId);
    if (!device) return;
    setBusy(true); setError("");
    try { setActive(await window.roadlinkDesktop!.trips.start({ deviceId, nickname: device.nickname, plate: device.plate, vehicle: device.vehicle })); }
    catch (reason) { setError(reason instanceof Error ? reason.message : String(reason)); }
    finally { setBusy(false); }
  };
  const stop = async () => {
    setBusy(true); setError("");
    try { const finished = await window.roadlinkDesktop!.trips.stop(); await refresh(); await chooseTrip(finished.id); }
    catch (reason) { setError(reason instanceof Error ? reason.message : String(reason)); }
    finally { setBusy(false); }
  };
  const sample = selected?.samples[cursor];
  const previous = selected?.samples[cursor - 1];
  const delta = sample && previous ? (sample.timestamp - previous.timestamp) / 1000 : 0;
  const acceleration = sample?.speed != null && previous?.speed != null && delta > 0 && delta <= 30 ? (sample.speed - previous.speed) / 3.6 / delta : null;
  return <section className="feature-page trips-page">
    <div className="feature-heading"><div><p className="eyebrow">Operations / Trips</p><h2>Trip recorder</h2><span>Record only while this app and its receiver are open. No extra device storage is used.</span></div><button className="secondary-button" onClick={() => void window.roadlinkDesktop?.trips.openFolder()}><FolderOpen size={15} /> Open trip logs</button></div>
    <div className="trip-controls panel"><label><span>RoadLink device</span><select value={deviceId} onChange={(event) => setDeviceId(event.target.value)} disabled={Boolean(active)}><option value="">Select a device</option>{devices.map((item) => <option key={item.id} value={item.id}>{item.nickname} · {item.id}</option>)}</select></label><div><strong>{active ? `Recording ${active.nickname}` : "Ready to record"}</strong><small>{active ? `${active.summary.sampleCount} telemetry samples · started ${formatTime(active.startedAt)}` : "Choose a connected device, then start."}</small></div><button className={active ? "stop-trip-button" : "primary-button"} disabled={busy || (!active && (!deviceId || demoMode || !window.roadlinkDesktop))} onClick={() => void (active ? stop() : start())}>{active ? <><Square size={15} /> End trip</> : <><Play size={15} /> Start trip</>}</button></div>
    {demoMode && <div className="feature-note">Trip recording uses authenticated live telemetry. Switch off Demo mode to record a physical RoadLink.</div>}
    {error && <div className="feature-error"><AlertTriangle size={15} /> {error}</div>}
    <div className="trip-layout"><div className="trip-list panel"><h3>Saved trips <span>{trips.length}</span></h3>{trips.map((trip) => <button className={`trip-list-item ${selected?.id === trip.id ? "is-selected" : ""}`} key={trip.id} onClick={() => void chooseTrip(trip.id)}><strong>{trip.nickname}</strong><span>{trip.plate} · {formatTime(trip.startedAt)}</span><small>{trip.summary.distanceKm.toFixed(2)} km · {duration(trip.summary.durationSeconds)} · {trip.summary.sampleCount} samples</small></button>)}{!trips.length && <div className="empty-state"><Navigation size={22} /><strong>No saved trips yet</strong><span>Finish a recording to see it here.</span></div>}</div>
      <div className="trip-review panel">{selected ? <><div className="trip-review-head"><div><p className="eyebrow">Recorded journey</p><h3>{selected.nickname}</h3><span>{selected.vehicle} · {selected.plate} · {selected.deviceId}</span></div><span>{formatTime(selected.startedAt)}</span></div><div className="trip-summary-grid"><div><Navigation size={16} /><small>Distance</small><strong>{selected.summary.distanceKm.toFixed(2)} km</strong></div><div><Gauge size={16} /><small>Top speed</small><strong>{selected.summary.topSpeedKmh} km/h</strong></div><div><Thermometer size={16} /><small>Max coolant</small><strong>{selected.summary.maxCoolantC}°C</strong></div><div><Clock size={16} /><small>Duration</small><strong>{duration(selected.summary.durationSeconds)}</strong></div></div><TripRouteMap samples={selected.samples} cursor={cursor} /><div className="trip-replay-title"><strong>Recording replay</strong><span>{sample ? formatTime(sample.timestamp) : "No telemetry samples"}</span></div><button className="trip-play-button" disabled={!selected.samples.length} onClick={() => { if (cursor >= selected.samples.length - 1) setCursor(0); setPlaying((value) => !value); }}>{playing ? <><Pause size={14} /> Pause</> : <><Play size={14} /> Play</>}</button><div className="trip-metric-tabs">{(["speed", "rpm", "coolant"] as const).map((item) => <button key={item} className={metric === item ? "is-active" : ""} onClick={() => setMetric(item)}>{item === "rpm" ? "RPM" : item}</button>)}</div><TripGraph samples={selected.samples} cursor={cursor} field={metric} onCursor={(index) => { setCursor(index); setPlaying(false); }} /><div className="trip-gauges"><div><Gauge size={16} /><small>Speed</small><strong>{sample?.speed == null ? "—" : `${Math.round(sample.speed)} km/h`}</strong></div><div><Activity size={16} /><small>Calculated acceleration</small><strong>{acceleration == null ? "—" : `${acceleration.toFixed(2)} m/s²`}</strong></div><div><Thermometer size={16} /><small>Coolant</small><strong>{sample?.coolant == null ? "—" : `${sample.coolant}°C`}</strong></div><div><Activity size={16} /><small>Engine</small><strong>{sample?.rpm == null ? "—" : `${Math.round(sample.rpm)} rpm`}</strong></div></div><small className="trip-calculation-note">Acceleration is estimated from consecutive speed samples. Replay advances sample by sample; gaps and missing readings are not interpolated.</small></> : <div className="empty-state"><Navigation size={28} /><strong>Select a saved trip</strong><span>Its route, summary, graph and replay gauges will appear here.</span></div>}</div></div>
  </section>;
}

export function AnalyticsPage() {
  const [trips, setTrips] = useState<TripRecord[]>([]);
  const [deviceId, setDeviceId] = useState("all");
  useEffect(() => { void window.roadlinkDesktop?.trips.list().then(setTrips); }, []);
  const ids = [...new Set(trips.map((trip) => trip.deviceId))];
  const filtered = useMemo(() => deviceId === "all" ? trips : trips.filter((trip) => trip.deviceId === deviceId), [trips, deviceId]);
  const distance = filtered.reduce((sum, trip) => sum + trip.summary.distanceKm, 0);
  const seconds = filtered.reduce((sum, trip) => sum + trip.summary.durationSeconds, 0);
  const topSpeed = Math.max(0, ...filtered.map((trip) => trip.summary.topSpeedKmh));
  return <section className="feature-page analytics-page"><div className="feature-heading"><div><p className="eyebrow">Operations / Analytics</p><h2>Fleet analytics</h2><span>Based on completed trips recorded by this computer.</span></div><label className="analytics-filter">Device <select value={deviceId} onChange={(event) => setDeviceId(event.target.value)}><option value="all">All RoadLinks</option>{ids.map((id) => <option key={id} value={id}>{id}</option>)}</select></label></div><div className="analytics-summary"><div><Navigation size={21} /><span>Trips</span><strong>{filtered.length}</strong></div><div><MapPin size={21} /><span>Total distance</span><strong>{distance.toFixed(1)} km</strong></div><div><Clock size={21} /><span>Recording time</span><strong>{duration(seconds)}</strong></div><div><Gauge size={21} /><span>Highest speed</span><strong>{topSpeed} km/h</strong></div></div><div className="analytics-history panel"><h3><BarChart3 size={19} /> Recorded activity</h3>{filtered.length ? filtered.map((trip) => <div className="analytics-row" key={trip.id}><div><strong>{trip.nickname}</strong><span>{trip.plate} · {formatTime(trip.startedAt)}</span></div><div className="analytics-bar"><i style={{ width: `${Math.max(2, trip.summary.distanceKm / Math.max(1, ...filtered.map((item) => item.summary.distanceKm)) * 100)}%` }} /></div><strong>{trip.summary.distanceKm.toFixed(2)} km</strong></div>) : <div className="empty-state"><BarChart3 size={25} /><strong>Nothing to analyze yet</strong><span>Complete a trip to build real reports. No demo data is counted.</span></div>}</div></section>;
}
