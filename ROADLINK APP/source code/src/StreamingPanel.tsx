import { Activity, BatteryCharging, Clock, Fuel, Gauge, MapPin, Power, Radio, Thermometer, Wind, X, Zap } from "lucide-react";
import { useEffect, useState } from "react";

const groups = [
  { name: "Engine", fields: [[0, "RPM", Gauge], [2, "Coolant", Thermometer], [3, "Throttle", Activity], [6, "Ignition timing", Zap]] },
  { name: "Air / Intake", fields: [[4, "Manifold pressure", Wind], [5, "Intake temperature", Thermometer]] },
  { name: "Driving", fields: [[1, "Speed", Gauge]] },
  { name: "Power / Fuel", fields: [[7, "ECU voltage", BatteryCharging], [8, "Fuel level", Fuel]] },
] as const;

export function StreamingPanel({ devices, receiverRunning, onState, onClose }: {
  devices: StreamingDevice[];
  receiverRunning: boolean;
  onState: (state: ReceiverState) => void;
  onClose: () => void;
}) {
  const [deviceId, setDeviceId] = useState(devices[0]?.id ?? "");
  const device = devices.find((entry) => entry.id === deviceId) ?? devices[0];
  const config = device?.desired ?? device?.applied;
  const [time, setTime] = useState(["00", "00", "10"]);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [now, setNow] = useState(Date.now());
  useEffect(() => { const timer = setInterval(() => setNow(Date.now()), 1000); return () => clearInterval(timer); }, []);
  useEffect(() => {
    const seconds = config?.interval_seconds ?? 10;
    setTime([Math.floor(seconds / 3600), Math.floor(seconds / 60) % 60, seconds % 60].map((value) => String(value).padStart(2, "0")));
    setError("");
  }, [device?.id, config?.revision, config?.interval_seconds]);

  async function update(patch: Partial<Omit<StreamingConfig, "revision">>) {
    const receiver = window.roadlinkDesktop?.receiver;
    if (!receiver || !device || !config) return;
    setBusy(true); setError("");
    try { onState(await receiver.configureStreaming(device.id, patch, config.revision)); }
    catch (reason) { setError(reason instanceof Error ? reason.message : String(reason)); }
    finally { setBusy(false); }
  }
  function saveInterval() {
    const [hours, minutes, seconds] = time.map(Number);
    const total = hours * 3600 + minutes * 60 + seconds;
    if (!time.every((value) => /^\d{1,2}$/.test(value)) || minutes > 59 || seconds > 59 || total < 1) {
      setError("Choose 00:00:01 to 99:59:59. Minutes and seconds must be 00–59."); return;
    }
    void update({ interval_seconds: total });
  }
  const online = device && now - Date.parse(device.lastSeenAt) < 90000;
  return <div className="modal-backdrop" onMouseDown={onClose}>
    <section className="modal stream-modal" role="dialog" aria-modal="true" aria-labelledby="stream-title" onMouseDown={(event) => event.stopPropagation()}>
      <header className="stream-header"><div><span className="eyebrow">ROADLINK CONTROL</span><h2 id="stream-title">Streaming</h2><p>Choose what your RoadLink sends, wherever it is installed.</p></div><button className="quiet-icon-button" aria-label="Close streaming" onClick={onClose}><X size={20} /></button></header>
      {!device || !config ? <div className="stream-empty"><Radio size={36} /><h3>Waiting for a RoadLink heartbeat</h3><p>Enable the receiver and configure its IP, port and access key on RoadLink. It can check in while telemetry is stopped.</p></div> : <>
        <div className="stream-device"><label>Device<select value={device.id} disabled={busy} onChange={(event) => setDeviceId(event.target.value)}>{devices.map((entry) => <option key={entry.id} value={entry.id}>{entry.id}</option>)}</select></label><span className={online && receiverRunning ? "stream-online" : "stream-offline"}>{!receiverRunning ? "Receiver stopped" : online ? "Device online" : "Device offline"}</span></div>
        {!device.stableIdentity && <p className="stream-notice">Prototype identified by its network address. Use one physical RoadLink per public address until the firmware provides unique IDs.</p>}
        <div className={`stream-sync ${device.desired ? "is-pending" : ""}`} role="status"><strong>{device.desired ? "Pending device confirmation" : device.status === "conflict" ? "Settings conflict — showing device values" : device.status === "changed-on-device" ? "Updated on RoadLink" : "Applied on RoadLink"}</strong><span>{device.desired ? "Saved on this computer. Delivered on the next heartbeat or telemetry request." : `Device revision ${device.applied.revision} · Last contact ${Math.max(0, Math.floor((now - Date.parse(device.lastSeenAt)) / 1000))}s ago`}</span></div>
        <div className="stream-actions"><button disabled={busy} className={`stream-power ${config.running ? "is-running" : ""}`} onClick={() => void update({ running: !config.running })}><Power size={21} /><span>{config.running ? "STOP STREAMING" : "START STREAMING"}<small>Device currently {device.applied.running ? "streaming" : "stopped"}</small></span></button><div className="stream-timer"><label><Clock size={15} /> Send interval</label><div>{time.map((value, index) => <label key={index}><input aria-label={["Hours", "Minutes", "Seconds"][index]} inputMode="numeric" maxLength={2} disabled={busy} value={value} onChange={(event) => setTime((current) => current.map((part, partIndex) => partIndex === index ? event.target.value.replace(/\D/g, "").slice(0, 2) : part))} /><small>{["HH", "MM", "SS"][index]}</small></label>)}<button disabled={busy} onClick={saveInterval}>Apply</button></div></div></div>
        <div className="stream-sources">{([["gps", "GPS location", MapPin], ["obd", "OBD-II data", Activity]] as const).map(([key, label, Icon]) => <button key={key} aria-pressed={config[key]} className={`stream-tile ${config[key] ? "is-on" : "is-off"}`} disabled={busy} onClick={() => void update({ [key]: !config[key] })}><Icon size={22} /><span>{label}</span><b>{config[key] ? "ON" : "OFF"}</b></button>)}</div>
        <div className="stream-field-heading"><h3>OBD-II fields</h3><span>{Array.from({ length: 9 }, (_, bit) => Number(Boolean(config.obd_fields & (1 << bit)))).reduce((a, b) => a + b, 0)} / 9 selected</span></div>
        {!config.obd && <p className="stream-notice">Field choices are saved. Enable OBD-II data to poll and transmit them.</p>}
        <div className="stream-groups">{groups.map((group) => <section key={group.name}><h4>{group.name}</h4><div>{group.fields.map(([bit, label, Icon]) => { const enabled = Boolean(config.obd_fields & (1 << bit)); return <button key={bit} disabled={busy} aria-pressed={enabled} className={`stream-tile ${enabled ? "is-on" : "is-off"}`} onClick={() => void update({ obd_fields: config.obd_fields ^ (1 << bit) })}><Icon size={20} /><span>{label}</span><b>{enabled ? "ON" : "OFF"}</b></button>; })}</div></section>)}</div>
        <p className="stream-footnote">Only selected OBD fields are polled. Heartbeats continue independently of START / STOP. Changes made with the rotary encoder appear here after the next check-in.</p>
      </>}
      {error && <p className="stream-error" role="alert">{error}</p>}
    </section>
  </div>;
}
