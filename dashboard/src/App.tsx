import {
  Activity,
  AlertTriangle,
  BarChart3,
  BatteryCharging,
  Bell,
  Car,
  Check,
  ChevronDown,
  Clock,
  Crosshair,
  Copy,
  Database,
  Edit3,
  Fuel,
  FolderOpen,
  Gauge,
  Globe2,
  KeyRound,
  Map as MapIcon,
  MapPin,
  MoreHorizontal,
  Navigation,
  Network,
  Plus,
  Power,
  Radio,
  RefreshCw,
  Route,
  Search,
  Server,
  Settings,
  ShieldCheck,
  Signal,
  Sliders,
  Thermometer,
  Wifi,
  WifiOff,
  X,
  Zap,
} from "lucide-react";
import { useEffect, useMemo, useRef, useState } from "react";
import type {
  LayerGroup,
  Map as LeafletMap,
  TileLayer,
} from "leaflet";

type DeviceStatus = "moving" | "parked" | "offline";
type ChartMetric = "speed" | "rpm" | "coolant";
type MapStyle = "street" | "dark" | "satellite";
type Accent = "lime" | "cyan" | "amber";

type TelemetrySample = {
  timestamp: number;
  speed: number;
  rpm: number;
  coolant: number;
  latitude?: number;
  longitude?: number;
};

type RoadLinkDevice = {
  id: string;
  nickname: string;
  vehicle: string;
  plate: string;
  imei: string;
  firmware: string;
  color: string;
  status: DeviceStatus;
  latitude: number;
  longitude: number;
  speed: number;
  rpm: number;
  coolant: number;
  battery: number;
  fuel: number;
  signal: number;
  odometer: number;
  pollRate: number;
  updatedAt: number;
  history: TelemetrySample[];
};

type AppSettings = {
  mapStyle: MapStyle;
  accent: Accent;
  units: "kmh" | "mph";
  compact: boolean;
  demoMode: boolean;
  mapTilerKey: string;
};

const DEVICE_STORE_KEY = "roadlink:fleet:devices:v1";
const SETTINGS_STORE_KEY = "roadlink:fleet:settings:v1";
const DEMO_REGION_STORE_KEY = "roadlink:fleet:demo-region:v1";
const LIVE_META_STORE_KEY = "roadlink:fleet:live-metadata:v1";

const pollOptions = [5, 15, 30, 60, 300];
const liveColors = ["#b8f34a", "#52d5ff", "#ffbd59", "#df8cff", "#ff7b66", "#68e0b0"];

type LiveDeviceMetadata = Record<string, Pick<RoadLinkDevice, "nickname" | "vehicle" | "plate" | "pollRate">>;

function readLiveMetadata(): LiveDeviceMetadata {
  try {
    return JSON.parse(localStorage.getItem(LIVE_META_STORE_KEY) ?? "{}") as LiveDeviceMetadata;
  } catch {
    return {};
  }
}

function saveLiveMetadata(devices: RoadLinkDevice[]) {
  const metadata = Object.fromEntries(devices.map((device) => [device.id, {
    nickname: device.nickname,
    vehicle: device.vehicle,
    plate: device.plate,
    pollRate: device.pollRate,
  }]));
  localStorage.setItem(LIVE_META_STORE_KEY, JSON.stringify(metadata));
}

function stableColor(value: string) {
  let hash = 0;
  for (const character of value) hash = ((hash << 5) - hash + character.charCodeAt(0)) | 0;
  return liveColors[Math.abs(hash) % liveColors.length];
}

function finiteNumber(value: unknown, fallback: number) {
  return typeof value === "number" && Number.isFinite(value) ? value : fallback;
}

function telemetryIdentity(event: RoadLinkTelemetry) {
  if (event.device_id?.trim()) return event.device_id.trim().toUpperCase();
  if (event.imei?.trim()) return `IMEI-${event.imei.trim()}`;
  if (event.device?.trim() && event.device.trim().toLowerCase() !== "roadlink") return event.device.trim().toUpperCase();
  const client = event._client.replace(/[^a-zA-Z0-9]+/g, "-").replace(/^-|-$/g, "");
  return `ROADLINK-${client || "UNKNOWN"}`.toUpperCase();
}

function mergeLiveTelemetry(current: RoadLinkDevice[], event: RoadLinkTelemetry) {
  const id = telemetryIdentity(event);
  const existing = current.find((device) => device.id === id);
  const saved = readLiveMetadata()[id];
  const receivedAt = Number.isFinite(Date.parse(event._received_at)) ? Date.parse(event._received_at) : Date.now();
  const speed = Math.max(0, finiteNumber(event.obd?.speed_kmh, finiteNumber(event.gps?.speed_kmh, existing?.speed ?? 0)));
  const rpm = Math.max(0, Math.round(finiteNumber(event.obd?.rpm, existing?.rpm ?? 0)));
  const coolant = Math.round(finiteNumber(event.obd?.coolant_c, existing?.coolant ?? 0));
  const signalFromRssi = event.modem?.rssi == null ? 0 : Math.max(1, Math.min(5, Math.ceil(event.modem.rssi / 6.2)));
  const signal = Math.round(finiteNumber(event.modem?.signal_bars, signalFromRssi || existing?.signal || 0));
  const latitude = finiteNumber(event.gps?.latitude, existing?.latitude ?? 20.6767);
  const longitude = finiteNumber(event.gps?.longitude, existing?.longitude ?? -103.3651);
  const sample = { timestamp: receivedAt, speed: Math.round(speed), rpm, coolant, latitude, longitude };

  const next: RoadLinkDevice = {
    id,
    nickname: existing?.nickname ?? saved?.nickname ?? id,
    vehicle: existing?.vehicle ?? saved?.vehicle ?? "Unassigned vehicle",
    plate: existing?.plate ?? saved?.plate ?? "No plate",
    imei: event.imei?.trim() || existing?.imei || "Not reported",
    firmware: event.firmware?.trim() || existing?.firmware || "RoadLink firmware",
    color: existing?.color ?? stableColor(id),
    status: speed > 1 ? "moving" : "parked",
    latitude,
    longitude,
    speed: Math.round(speed),
    rpm,
    coolant,
    battery: finiteNumber(event.obd?.voltage_v, existing?.battery ?? 0),
    fuel: Math.round(finiteNumber(event.obd?.fuel_pct, existing?.fuel ?? 0)),
    signal,
    odometer: finiteNumber(event.obd?.odometer_km, existing?.odometer ?? 0),
    pollRate: existing?.pollRate ?? saved?.pollRate ?? 30,
    updatedAt: receivedAt,
    history: [...(existing?.history ?? []).slice(-179), sample],
  };
  return existing ? current.map((device) => device.id === id ? next : device) : [...current, next];
}

function seededHistory(speed: number, rpm: number, coolant: number) {
  return Array.from({ length: 30 }, (_, index) => {
    const offset = index - 29;
    const wave = Math.sin(index * 0.54) * 7 + Math.cos(index * 0.23) * 4;
    return {
      timestamp: Date.now() + offset * 60_000,
      speed: Math.max(0, Math.round(speed + wave)),
      rpm: Math.max(720, Math.round(rpm + wave * 31)),
      coolant: Math.round(coolant + Math.sin(index * 0.3) * 2),
    };
  });
}

const seedDevices: RoadLinkDevice[] = [
  {
    id: "RL-1007",
    nickname: "Delivery Van 07",
    vehicle: "2021 Ford Transit",
    plate: "RDL-107-A",
    imei: "867997060451282",
    firmware: "RoadLink 0.9.3",
    color: "#b8f34a",
    status: "moving",
    latitude: 20.6748,
    longitude: -103.3475,
    speed: 46,
    rpm: 2180,
    coolant: 91,
    battery: 13.9,
    fuel: 68,
    signal: 4,
    odometer: 84631,
    pollRate: 30,
    updatedAt: Date.now() - 800,
    history: seededHistory(42, 2100, 90),
  },
  {
    id: "RL-1012",
    nickname: "Service Hilux",
    vehicle: "2020 Toyota Hilux",
    plate: "RDL-212-B",
    imei: "867997060451399",
    firmware: "RoadLink 0.9.3",
    color: "#52d5ff",
    status: "moving",
    latitude: 20.6676,
    longitude: -103.3652,
    speed: 31,
    rpm: 1740,
    coolant: 88,
    battery: 14.1,
    fuel: 42,
    signal: 3,
    odometer: 121205,
    pollRate: 15,
    updatedAt: Date.now() - 3400,
    history: seededHistory(35, 1820, 88),
  },
  {
    id: "RL-1024",
    nickname: "Civic Shop Car",
    vehicle: "2018 Honda Civic",
    plate: "RDL-024-C",
    imei: "867997060451480",
    firmware: "RoadLink 0.9.2",
    color: "#ffbd59",
    status: "parked",
    latitude: 20.6916,
    longitude: -103.3822,
    speed: 0,
    rpm: 0,
    coolant: 73,
    battery: 12.6,
    fuel: 81,
    signal: 5,
    odometer: 67492,
    pollRate: 60,
    updatedAt: Date.now() - 11800,
    history: seededHistory(0, 780, 75),
  },
  {
    id: "RL-1031",
    nickname: "Track Demo",
    vehicle: "2016 Volkswagen GTI",
    plate: "DEMO-031",
    imei: "867997060451555",
    firmware: "RoadLink 0.9.3",
    color: "#df8cff",
    status: "parked",
    latitude: 20.6597,
    longitude: -103.3494,
    speed: 0,
    rpm: 0,
    coolant: 82,
    battery: 12.8,
    fuel: 54,
    signal: 4,
    odometer: 93217,
    pollRate: 30,
    updatedAt: Date.now() - 6100,
    history: seededHistory(0, 820, 82),
  },
  {
    id: "RL-1045",
    nickname: "Warehouse Pickup",
    vehicle: "2019 Nissan NP300",
    plate: "RDL-145-D",
    imei: "867997060451644",
    firmware: "RoadLink 0.9.1",
    color: "#7e8b94",
    status: "offline",
    latitude: 20.7212,
    longitude: -103.3908,
    speed: 0,
    rpm: 0,
    coolant: 42,
    battery: 11.9,
    fuel: 26,
    signal: 0,
    odometer: 155028,
    pollRate: 300,
    updatedAt: Date.now() - 52 * 60_000,
    history: seededHistory(0, 0, 43),
  },
];

const defaultSettings: AppSettings = {
  mapStyle: "dark",
  accent: "lime",
  units: "kmh",
  compact: false,
  demoMode: true,
  mapTilerKey: import.meta.env.VITE_MAPTILER_KEY ?? "",
};

function readDevices() {
  try {
    const saved = localStorage.getItem(DEVICE_STORE_KEY);
    if (saved) {
      const devices = JSON.parse(saved) as RoadLinkDevice[];
      if (localStorage.getItem(DEMO_REGION_STORE_KEY) !== "guadalajara") {
        const knownPositions = new Map(seedDevices.map((device) => [device.id, [device.latitude, device.longitude]]));
        const relocated = devices.map((device, index) => {
          const known = knownPositions.get(device.id);
          return {
            ...device,
            latitude: known?.[0] ?? 20.663 + (index % 5) * 0.0052,
            longitude: known?.[1] ?? -103.381 + (index % 4) * 0.0081,
          };
        });
        localStorage.setItem(DEVICE_STORE_KEY, JSON.stringify(relocated));
        localStorage.setItem(DEMO_REGION_STORE_KEY, "guadalajara");
        return relocated;
      }
      return devices;
    }
    localStorage.setItem(DEMO_REGION_STORE_KEY, "guadalajara");
  } catch {
    // A corrupt preference should never prevent the fleet view from opening.
  }
  return seedDevices;
}

function readSettings() {
  try {
    const saved = localStorage.getItem(SETTINGS_STORE_KEY);
    if (saved) {
      const settings = { ...defaultSettings, ...JSON.parse(saved) } as AppSettings;
      if (settings.mapStyle === "satellite" && !settings.mapTilerKey.trim()) {
        settings.mapStyle = "dark";
      }
      return settings;
    }
  } catch {
    // Fall back to safe defaults.
  }
  return defaultSettings;
}

function saveDevices(devices: RoadLinkDevice[]) {
  localStorage.setItem(DEVICE_STORE_KEY, JSON.stringify(devices));
}

function secondsLabel(seconds: number) {
  if (seconds < 60) return `${seconds}s`;
  return `${Math.round(seconds / 60)}m`;
}

function timeAgo(timestamp: number, now: number) {
  const seconds = Math.max(0, Math.floor((now - timestamp) / 1000));
  if (seconds < 4) return "just now";
  if (seconds < 60) return `${seconds}s ago`;
  const minutes = Math.floor(seconds / 60);
  if (minutes < 60) return `${minutes}m ago`;
  return `${Math.floor(minutes / 60)}h ago`;
}

function tileSource(style: MapStyle, mapTilerKey: string) {
  if (style === "satellite" && mapTilerKey.trim()) {
    return {
      url: `https://api.maptiler.com/maps/satellite-v4/256/{z}/{x}/{y}.jpg?key=${encodeURIComponent(mapTilerKey.trim())}`,
      attribution: "&copy; MapTiler &copy; OpenStreetMap contributors",
    };
  }
  if (style === "street") {
    return {
      url: "https://{s}.basemaps.cartocdn.com/rastertiles/voyager/{z}/{x}/{y}{r}.png",
      attribution: "&copy; OpenStreetMap &copy; CARTO",
    };
  }
  return {
    url: "https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png",
    attribution: "&copy; OpenStreetMap &copy; CARTO",
  };
}

function RoadLinkMap({
  devices,
  selectedId,
  mapStyle,
  mapTilerKey,
  onSelect,
}: {
  devices: RoadLinkDevice[];
  selectedId: string;
  mapStyle: MapStyle;
  mapTilerKey: string;
  onSelect: (id: string) => void;
}) {
  const containerRef = useRef<HTMLDivElement>(null);
  const mapRef = useRef<LeafletMap | null>(null);
  const markersRef = useRef<LayerGroup | null>(null);
  const tileRef = useRef<TileLayer | null>(null);
  const leafletRef = useRef<typeof import("leaflet") | null>(null);

  useEffect(() => {
    let disposed = false;

    void (async () => {
      const L = await import("leaflet");
      if (disposed || !containerRef.current || mapRef.current) return;
      leafletRef.current = L;

      const map = L.map(containerRef.current, {
        zoomControl: false,
        attributionControl: true,
      }).setView([20.6767, -103.3651], 13);

      L.control.zoom({ position: "bottomright" }).addTo(map);
      const source = tileSource(mapStyle, mapTilerKey);
      tileRef.current = L.tileLayer(source.url, {
        maxZoom: 20,
        attribution: source.attribution,
      }).addTo(map);
      markersRef.current = L.layerGroup().addTo(map);
      mapRef.current = map;
      window.setTimeout(() => map.invalidateSize(), 80);
    })();

    return () => {
      disposed = true;
      mapRef.current?.remove();
      mapRef.current = null;
      markersRef.current = null;
      tileRef.current = null;
    };
  }, []);

  useEffect(() => {
    const L = leafletRef.current;
    const map = mapRef.current;
    if (!L || !map || !tileRef.current) return;
    tileRef.current.remove();
    const source = tileSource(mapStyle, mapTilerKey);
    tileRef.current = L.tileLayer(source.url, {
      maxZoom: 20,
      attribution: source.attribution,
    }).addTo(map);
    tileRef.current.bringToBack();
  }, [mapStyle, mapTilerKey]);

  useEffect(() => {
    const L = leafletRef.current;
    const layer = markersRef.current;
    if (!L || !layer) return;
    layer.clearLayers();

    devices.forEach((device) => {
      if (device.id === selectedId && device.history.length > 1) {
        const actualPath = device.history
          .slice(-80)
          .filter((sample) => sample.latitude != null && sample.longitude != null)
          .map((sample) => [sample.latitude as number, sample.longitude as number] as [number, number]);
        const path = actualPath.length > 1 ? actualPath : device.history.slice(-12).map((_, index, samples) => {
          const distance = samples.length - index;
          return [device.latitude - distance * 0.00028, device.longitude - distance * 0.00034] as [number, number];
        });
        if (path.at(-1)?.[0] !== device.latitude || path.at(-1)?.[1] !== device.longitude) path.push([device.latitude, device.longitude]);
        L.polyline(path, {
          color: device.color,
          weight: 3,
          opacity: 0.7,
          dashArray: "2 8",
        }).addTo(layer);
      }

      const isSelected = device.id === selectedId;
      const marker = L.circleMarker([device.latitude, device.longitude], {
        radius: isSelected ? 11 : 8,
        color: isSelected ? "#ffffff" : device.color,
        weight: isSelected ? 3 : 2,
        fillColor: device.status === "offline" ? "#59636a" : device.color,
        fillOpacity: device.status === "offline" ? 0.55 : 0.95,
        className: device.status === "moving" ? "moving-marker" : "",
      }).addTo(layer);

      const tooltip = document.createElement("div");
      tooltip.className = "map-tooltip";
      const name = document.createElement("strong");
      name.textContent = device.nickname;
      const meta = document.createElement("span");
      meta.textContent = `${device.speed} km/h · ${device.id}`;
      tooltip.append(name, meta);
      marker.bindTooltip(tooltip, {
        direction: "top",
        offset: [0, -10],
        opacity: 1,
      });
      marker.on("click", () => onSelect(device.id));
    });
  }, [devices, selectedId, onSelect]);

  useEffect(() => {
    const selected = devices.find((device) => device.id === selectedId);
    if (!selected || !mapRef.current) return;
    mapRef.current.flyTo(
      [selected.latitude, selected.longitude],
      Math.max(mapRef.current.getZoom(), 14),
      { duration: 0.65 },
    );
  }, [selectedId]);

  return <div className="map-canvas" ref={containerRef} aria-label="Live fleet map" />;
}

function TelemetryChart({
  samples,
  metric,
  color,
}: {
  samples: TelemetrySample[];
  metric: ChartMetric;
  color: string;
}) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const render = () => {
      const bounds = canvas.getBoundingClientRect();
      const ratio = window.devicePixelRatio || 1;
      canvas.width = Math.max(1, Math.floor(bounds.width * ratio));
      canvas.height = Math.max(1, Math.floor(bounds.height * ratio));
      const context = canvas.getContext("2d");
      if (!context) return;
      context.scale(ratio, ratio);

      const width = bounds.width;
      const height = bounds.height;
      const inset = { top: 10, right: 5, bottom: 9, left: 5 };
      const chartHeight = height - inset.top - inset.bottom;
      const values = samples.map((sample) => sample[metric]);
      const min = Math.min(...values);
      const max = Math.max(...values);
      const range = Math.max(1, max - min);

      context.clearRect(0, 0, width, height);
      context.strokeStyle = "rgba(255,255,255,0.07)";
      context.lineWidth = 1;
      for (let row = 0; row < 4; row += 1) {
        const y = inset.top + (chartHeight * row) / 3;
        context.beginPath();
        context.moveTo(0, y);
        context.lineTo(width, y);
        context.stroke();
      }

      const points = values.map((value, index) => ({
        x: inset.left + (index / Math.max(1, values.length - 1)) * (width - inset.left - inset.right),
        y: inset.top + (1 - (value - min) / range) * chartHeight,
      }));

      if (points.length < 2) return;
      const gradient = context.createLinearGradient(0, 0, 0, height);
      gradient.addColorStop(0, `${color}3b`);
      gradient.addColorStop(1, `${color}00`);
      context.beginPath();
      context.moveTo(points[0].x, height);
      points.forEach((point) => context.lineTo(point.x, point.y));
      context.lineTo(points[points.length - 1].x, height);
      context.closePath();
      context.fillStyle = gradient;
      context.fill();

      context.beginPath();
      points.forEach((point, index) => {
        if (index === 0) context.moveTo(point.x, point.y);
        else context.lineTo(point.x, point.y);
      });
      context.strokeStyle = color;
      context.lineWidth = 2;
      context.lineJoin = "round";
      context.lineCap = "round";
      context.stroke();

      const last = points[points.length - 1];
      context.beginPath();
      context.arc(last.x, last.y, 3.5, 0, Math.PI * 2);
      context.fillStyle = color;
      context.fill();
      context.strokeStyle = "#0e1418";
      context.lineWidth = 2;
      context.stroke();
    };

    render();
    const observer = new ResizeObserver(render);
    observer.observe(canvas);
    return () => observer.disconnect();
  }, [samples, metric, color]);

  return <canvas className="telemetry-chart" ref={canvasRef} />;
}

function Metric({
  icon,
  label,
  value,
  detail,
}: {
  icon: React.ReactNode;
  label: string;
  value: string;
  detail?: string;
}) {
  return (
    <div className="metric-card">
      <span className="metric-icon">{icon}</span>
      <div>
        <span>{label}</span>
        <strong>{value}</strong>
        {detail && <small>{detail}</small>}
      </div>
    </div>
  );
}

function StatusDot({ status }: { status: DeviceStatus }) {
  return <span className={`status-dot status-${status}`} aria-label={status} />;
}

export default function App() {
  const [devices, setDevices] = useState<RoadLinkDevice[]>(readDevices);
  const [liveDevices, setLiveDevices] = useState<RoadLinkDevice[]>([]);
  const [settings, setSettings] = useState<AppSettings>(readSettings);
  const [selectedId, setSelectedId] = useState(devices[0]?.id ?? "");
  const [focusedId, setFocusedId] = useState(devices[0]?.id ?? "");
  const [search, setSearch] = useState("");
  const [chartMetric, setChartMetric] = useState<ChartMetric>("speed");
  const [now, setNow] = useState(Date.now());
  const [editId, setEditId] = useState<string | null>(null);
  const [showAdd, setShowAdd] = useState(false);
  const [showSettings, setShowSettings] = useState(false);
  const [showReceiver, setShowReceiver] = useState(false);
  const [receiverState, setReceiverState] = useState<ReceiverState | null>(null);
  const [receiverPortDraft, setReceiverPortDraft] = useState("8080");
  const [receiverBusy, setReceiverBusy] = useState(false);
  const [toast, setToast] = useState<string | null>(null);
  const [editDraft, setEditDraft] = useState({ nickname: "", vehicle: "", plate: "", pollRate: 30 });
  const [addDraft, setAddDraft] = useState({ id: "", nickname: "", vehicle: "", imei: "", pollRate: 30 });

  const activeDevices = useMemo(() => settings.demoMode ? devices : liveDevices.map((device) => ({
    ...device,
    status: now - device.updatedAt > Math.max(90_000, device.pollRate * 3000) ? "offline" as const : device.status,
  })), [devices, liveDevices, now, settings.demoMode]);
  const selected = activeDevices.find((device) => device.id === selectedId) ?? activeDevices[0];
  const filteredDevices = useMemo(() => {
    const query = search.trim().toLowerCase();
    if (!query) return activeDevices;
    return activeDevices.filter((device) =>
      [device.nickname, device.vehicle, device.id, device.plate]
        .join(" ")
        .toLowerCase()
        .includes(query),
    );
  }, [activeDevices, search]);

  const movingCount = activeDevices.filter((device) => device.status === "moving").length;
  const onlineCount = activeDevices.filter((device) => device.status !== "offline").length;

  useEffect(() => {
    const clock = window.setInterval(() => setNow(Date.now()), 1000);
    return () => window.clearInterval(clock);
  }, []);

  useEffect(() => {
    localStorage.setItem(SETTINGS_STORE_KEY, JSON.stringify(settings));
  }, [settings]);

  useEffect(() => {
    const receiver = window.roadlinkDesktop?.receiver;
    if (!receiver) return;
    void receiver.getState().then((state) => {
      setReceiverState(state);
      setReceiverPortDraft(String(state.port));
    });
    const removeStateListener = receiver.onState((state) => {
      setReceiverState(state);
      setReceiverPortDraft(String(state.port));
    });
    const removeTelemetryListener = receiver.onTelemetry((telemetry) => {
      const identity = telemetryIdentity(telemetry);
      setLiveDevices((current) => {
        const next = mergeLiveTelemetry(current, telemetry);
        if (!settings.demoMode) {
          setSelectedId((selectedDevice) => next.some((device) => device.id === selectedDevice) ? selectedDevice : identity);
          setFocusedId((focusedDevice) => next.some((device) => device.id === focusedDevice) ? focusedDevice : identity);
        }
        return next;
      });
    });
    return () => {
      removeStateListener();
      removeTelemetryListener();
    };
  }, [settings.demoMode]);

  useEffect(() => {
    if (!settings.demoMode) return;
    const simulator = window.setInterval(() => {
      const currentTime = Date.now();
      setDevices((current) =>
        current.map((device) => {
          if (device.status === "offline") return device;
          const effectivePoll = device.id === focusedId ? 1 : device.pollRate;
          if (currentTime - device.updatedAt < effectivePoll * 1000) return device;

          const moving = device.status === "moving";
          const speed = moving
            ? Math.max(8, Math.min(92, Math.round(device.speed + (Math.random() - 0.5) * 12)))
            : 0;
          const rpm = moving
            ? Math.max(900, Math.round(1250 + speed * 19 + (Math.random() - 0.5) * 180))
            : 0;
          const coolant = Math.max(72, Math.min(103, Math.round(device.coolant + (Math.random() - 0.48) * 2)));
          const nextSample = { timestamp: currentTime, speed, rpm, coolant };
          return {
            ...device,
            speed,
            rpm,
            coolant,
            latitude: moving ? device.latitude + (Math.random() - 0.36) * 0.00024 : device.latitude,
            longitude: moving ? device.longitude + (Math.random() - 0.48) * 0.00024 : device.longitude,
            signal: Math.max(1, Math.min(5, device.signal + (Math.random() > 0.88 ? (Math.random() > 0.5 ? 1 : -1) : 0))),
            updatedAt: currentTime,
            history: [...device.history.slice(-35), nextSample],
          };
        }),
      );
    }, 1000);
    return () => window.clearInterval(simulator);
  }, [focusedId, settings.demoMode]);

  useEffect(() => {
    if (!toast) return;
    const timer = window.setTimeout(() => setToast(null), 2800);
    return () => window.clearTimeout(timer);
  }, [toast]);

  const displaySpeed = (speed: number) => {
    if (settings.units === "mph") return `${Math.round(speed * 0.621371)} mph`;
    return `${speed} km/h`;
  };

  const selectDevice = (id: string) => {
    setSelectedId(id);
    setFocusedId(id);
  };

  const updatePollRate = (id: string, pollRate: number) => {
    if (settings.demoMode) {
      setDevices((current) => {
        const next = current.map((device) => (device.id === id ? { ...device, pollRate } : device));
        saveDevices(next);
        return next;
      });
      setToast(`Normal reporting set to ${secondsLabel(pollRate)}`);
    } else {
      setLiveDevices((current) => {
        const next = current.map((device) => (device.id === id ? { ...device, pollRate } : device));
        saveLiveMetadata(next);
        return next;
      });
      setToast(`Saved ${secondsLabel(pollRate)} as the desired rate; firmware command delivery is not available yet`);
    }
  };

  const openEditor = (device: RoadLinkDevice) => {
    setEditDraft({
      nickname: device.nickname,
      vehicle: device.vehicle,
      plate: device.plate,
      pollRate: device.pollRate,
    });
    setEditId(device.id);
  };

  const saveEditor = () => {
    if (!editId || !editDraft.nickname.trim()) return;
    const update = (current: RoadLinkDevice[]) => {
      const next = current.map((device) =>
        device.id === editId
          ? {
              ...device,
              nickname: editDraft.nickname.trim(),
              vehicle: editDraft.vehicle.trim() || "Unassigned vehicle",
              plate: editDraft.plate.trim() || "No plate",
              pollRate: editDraft.pollRate,
            }
          : device,
      );
      if (settings.demoMode) saveDevices(next);
      else saveLiveMetadata(next);
      return next;
    };
    if (settings.demoMode) setDevices(update);
    else setLiveDevices(update);
    setEditId(null);
    setToast("RoadLink details saved");
  };

  const addDevice = () => {
    const cleanId = addDraft.id.trim().toUpperCase();
    if (!cleanId || !addDraft.nickname.trim() || devices.some((device) => device.id === cleanId)) {
      setToast(devices.some((device) => device.id === cleanId) ? "That device ID already exists" : "Device ID and nickname are required");
      return;
    }
    const newDevice: RoadLinkDevice = {
      id: cleanId,
      nickname: addDraft.nickname.trim(),
      vehicle: addDraft.vehicle.trim() || "Unassigned vehicle",
      plate: "No plate",
      imei: addDraft.imei.trim() || "Not paired",
      firmware: "Waiting for device",
      color: "#ff7b66",
      status: "parked",
      latitude: 20.663 + Math.random() * 0.026,
      longitude: -103.381 + Math.random() * 0.035,
      speed: 0,
      rpm: 0,
      coolant: 24,
      battery: 0,
      fuel: 0,
      signal: 0,
      odometer: 0,
      pollRate: addDraft.pollRate,
      updatedAt: Date.now(),
      history: seededHistory(0, 0, 24),
    };
    setDevices((current) => {
      const next = [...current, newDevice];
      saveDevices(next);
      return next;
    });
    setSelectedId(newDevice.id);
    setFocusedId(newDevice.id);
    setShowAdd(false);
    setAddDraft({ id: "", nickname: "", vehicle: "", imei: "", pollRate: 30 });
    setToast(`${newDevice.nickname} added to the fleet`);
  };

  const resetDemo = () => {
    const reset = seedDevices.map((device) => ({ ...device, updatedAt: Date.now() - 1200 }));
    setDevices(reset);
    saveDevices(reset);
    localStorage.setItem(DEMO_REGION_STORE_KEY, "guadalajara");
    setSelectedId(reset[0].id);
    setFocusedId(reset[0].id);
    setToast("Demo fleet restored");
  };

  const toggleDemoMode = () => {
    const enabled = !settings.demoMode;
    setSettings((current) => ({ ...current, demoMode: enabled }));
    if (enabled && devices[0]) {
      setSelectedId(devices[0].id);
      setFocusedId(devices[0].id);
    } else if (!enabled && liveDevices[0]) {
      setSelectedId(liveDevices[0].id);
      setFocusedId(liveDevices[0].id);
    } else {
      setSelectedId("");
      setFocusedId("");
    }
    setToast(enabled ? "Demo fleet enabled" : "Demo fleet disabled; showing authenticated receiver data");
  };

  const runReceiverAction = async (action: () => Promise<ReceiverState>, success: string) => {
    setReceiverBusy(true);
    try {
      const state = await action();
      setReceiverState(state);
      setReceiverPortDraft(String(state.port));
      setToast(success);
    } catch (error) {
      setToast(error instanceof Error ? error.message : "Receiver action failed");
    } finally {
      setReceiverBusy(false);
    }
  };

  const applyReceiverPort = () => {
    const port = Number(receiverPortDraft);
    if (!Number.isInteger(port) || port < 1 || port > 65535) {
      setToast("Enter a TCP port from 1 to 65535");
      return;
    }
    const receiver = window.roadlinkDesktop?.receiver;
    if (receiver) void runReceiverAction(() => receiver.update({ port, enabled: true }), `Receiver restarted on port ${port}`);
  };

  const copyValue = (value: string, label: string) => {
    window.roadlinkDesktop?.copyText(value);
    setToast(`${label} copied`);
  };

  const addFirewallRule = async () => {
    const receiver = window.roadlinkDesktop?.receiver;
    if (!receiver) return;
    setReceiverBusy(true);
    try {
      await receiver.addFirewallRule();
      setToast("Windows Firewall private-network rule added");
    } catch (error) {
      setToast(error instanceof Error ? error.message : "Firewall rule was not added");
    } finally {
      setReceiverBusy(false);
    }
  };

  const cycleMapStyle = () => {
    const styles: MapStyle[] = settings.mapTilerKey.trim()
      ? ["dark", "street", "satellite"]
      : ["dark", "street"];
    const currentIndex = styles.indexOf(settings.mapStyle);
    const mapStyle = styles[(currentIndex + 1) % styles.length];
    setSettings((current) => ({ ...current, mapStyle }));
  };

  const chartValue = !selected ? "" : chartMetric === "speed" ? displaySpeed(selected.speed) : chartMetric === "rpm" ? `${selected.rpm.toLocaleString()} rpm` : `${selected.coolant}°C`;
  const chartLabel = chartMetric === "speed" ? "Speed" : chartMetric === "rpm" ? "Engine speed" : "Coolant";
  const focused = Boolean(selected && focusedId === selected.id);
  const receiverHealthy = receiverState?.running === true;
  const sourceLabel = settings.demoMode ? "Demo simulator" : receiverHealthy ? "Receiver listening" : "Receiver stopped";

  return (
    <div className={`app-shell ${settings.compact ? "is-compact" : ""}`} data-accent={settings.accent}>
      <div className="titlebar">
        <div className="titlebar-product">
          <span className="mini-mark"><Route size={14} /></span>
          <span>RoadLink Fleet</span>
          <small>Desktop receiver</small>
        </div>
        <div className="titlebar-status"><span className={settings.demoMode || receiverHealthy ? "live-pip" : "offline-pip"} /> {sourceLabel}</div>
      </div>

      <aside className="sidebar">
        <div className="brand">
          <div className="brand-mark"><Route size={25} strokeWidth={2.4} /></div>
          <div><strong>ROADLINK</strong><span>Fleet control</span></div>
        </div>

        <nav className="primary-nav" aria-label="Primary navigation">
          <button className="nav-item is-active"><MapIcon size={18} /><span>Fleet overview</span></button>
          <button className="nav-item" onClick={() => setToast("Trip history will use the same telemetry store in the next milestone")}><Navigation size={18} /><span>Trips</span><small>Soon</small></button>
          <button className="nav-item" onClick={() => setToast("Fleet-wide reports are planned after live ingestion")}><BarChart3 size={18} /><span>Analytics</span><small>Soon</small></button>
          <button className="nav-item" onClick={() => setToast(settings.demoMode ? "All demo devices are visible in the fleet list" : `${liveDevices.length} authenticated live device${liveDevices.length === 1 ? "" : "s"}`)}><Database size={18} /><span>Devices</span><span className="nav-count">{activeDevices.length}</span></button>
          <button className="nav-item" onClick={() => setShowReceiver(true)}><Network size={18} /><span>Receiver</span><small>{receiverHealthy ? "Live" : "Open"}</small></button>
        </nav>

        <div className="sidebar-spacer" />
        <button className="ingest-card ingest-card-button" onClick={() => setShowReceiver(true)}>
          <div className="ingest-card-head"><Server size={16} /><span>Ingest service</span></div>
          <strong>{receiverHealthy ? `TCP ${receiverState?.port}` : "Receiver stopped"}</strong>
          <p>{receiverState?.publicEndpoint ? `${receiverState.mapping?.method} public endpoint ready.` : "Local HTTP receiver for RoadLink LTE packets."}</p>
          <div className={`ingest-health ${receiverHealthy ? "" : "is-offline"}`}><span /><span>{receiverHealthy ? "Listening" : "Not listening"}</span><b>{receiverState?.packetCount ?? 0} pkt</b></div>
        </button>
        <button className="nav-item settings-button" onClick={() => setShowSettings(true)}><Settings size={18} /><span>Preferences</span></button>
        <div className="version-line">RoadLink Fleet v{window.roadlinkDesktop?.version ?? "0.1.0"}</div>
      </aside>

      <main className="main-content">
        <header className="page-header">
          <div>
            <p className="eyebrow">Operations / Live fleet</p>
            <h1>{settings.demoMode ? "Good evening, fleet is moving." : liveDevices.length ? "Live RoadLinks are reporting." : "Receiver ready for a RoadLink."}</h1>
          </div>
          <div className="header-actions">
            <div className="sync-state"><span className={settings.demoMode || receiverHealthy ? "live-pip" : "offline-pip"} /><div><strong>{settings.demoMode ? "Demo live" : receiverHealthy ? "Receiver live" : "Receiver off"}</strong><small>{new Date(now).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" })}</small></div></div>
            <button className="icon-button" aria-label="Notifications"><Bell size={18} /><span className="notification-dot" /></button>
            <button className="primary-button" onClick={() => settings.demoMode ? setShowAdd(true) : setShowReceiver(true)}>{settings.demoMode ? <Plus size={17} /> : <Network size={17} />} {settings.demoMode ? "Add RoadLink" : "Receiver setup"}</button>
          </div>
        </header>

        <section className="fleet-stats" aria-label="Fleet summary">
          <div className="stat-card"><span className="stat-icon"><Wifi size={18} /></span><div><small>Online now</small><strong>{onlineCount}<span> / {activeDevices.length}</span></strong></div><em className={settings.demoMode || receiverHealthy ? "positive" : ""}>{settings.demoMode ? "Demo live" : receiverHealthy ? "Receiver live" : "Stopped"}</em></div>
          <div className="stat-card"><span className="stat-icon blue"><Navigation size={18} /></span><div><small>On the move</small><strong>{movingCount}<span> vehicles</span></strong></div><em>{activeDevices.length ? Math.round((movingCount / activeDevices.length) * 100) : 0}% of fleet</em></div>
          <div className="stat-card"><span className="stat-icon amber"><AlertTriangle size={18} /></span><div><small>Needs attention</small><strong>{settings.demoMode ? 1 : activeDevices.length - onlineCount}<span> device</span></strong></div><em className={settings.demoMode ? "warning" : ""}>{settings.demoMode ? "Offline 52m" : receiverState?.lastError ? "Receiver warning" : "No alerts"}</em></div>
          <div className="stat-card"><span className="stat-icon violet"><Radio size={18} /></span><div><small>Received this run</small><strong>{settings.demoMode ? "18.4k" : receiverState?.packetCount ?? 0}</strong></div><em className="positive">{settings.demoMode ? "+12.8%" : receiverState?.lastPacketAt ? "Authenticated" : "Waiting"}</em></div>
        </section>

        <section className="fleet-workspace">
          {selected ? <>
          <div className="device-panel panel">
            <div className="panel-heading">
              <div><h2>RoadLinks</h2><span>{onlineCount} reporting</span></div>
              <button className="quiet-icon-button" aria-label="Device filters"><Sliders size={16} /></button>
            </div>
            <label className="search-field"><Search size={16} /><input value={search} onChange={(event) => setSearch(event.target.value)} placeholder="Search device or vehicle" /><kbd>⌘ K</kbd></label>
            <div className="device-list">
              {filteredDevices.map((device) => {
                const isSelected = selected.id === device.id;
                const isFocused = focusedId === device.id;
                return (
                  <button key={device.id} className={`device-card ${isSelected ? "is-selected" : ""}`} onClick={() => selectDevice(device.id)}>
                    <div className="device-card-top">
                      <div className="vehicle-avatar" style={{ "--device-color": device.color } as React.CSSProperties}><Car size={18} /></div>
                      <div className="device-name"><strong>{device.nickname}</strong><span><StatusDot status={device.status} /> {device.status} · {timeAgo(device.updatedAt, now)}</span></div>
                      <span className="device-speed">{device.status === "moving" ? displaySpeed(device.speed) : "—"}</span>
                    </div>
                    <div className="device-card-bottom"><span>{device.vehicle}</span><span>{isFocused && settings.demoMode ? <><Zap size={12} /> 1s focus</> : <><Clock size={12} /> {secondsLabel(device.pollRate)}</>}</span></div>
                  </button>
                );
              })}
              {filteredDevices.length === 0 && <div className="empty-state"><Search size={22} /><strong>No RoadLinks found</strong><span>Try a nickname, plate, or device ID.</span></div>}
            </div>
            {settings.demoMode && <button className="add-device-inline" onClick={() => setShowAdd(true)}><Plus size={15} /> Add a demo RoadLink</button>}
          </div>

          <div className="map-panel panel">
            <RoadLinkMap devices={activeDevices} selectedId={selected.id} mapStyle={settings.mapStyle} mapTilerKey={settings.mapTilerKey} onSelect={selectDevice} />
            <div className="map-topbar">
              <div className="map-title"><span className="live-pip" /><div><strong>Live fleet map</strong><small>{settings.demoMode ? "Guadalajara demo" : "Authenticated receiver"} · {onlineCount} active units</small></div></div>
              <div className="map-actions">
                <button className="map-style-button" onClick={cycleMapStyle}><MapIcon size={15} /> {settings.mapStyle[0].toUpperCase() + settings.mapStyle.slice(1)}<ChevronDown size={14} /></button>
                <button className="map-round-button" aria-label="Center selected device" onClick={() => selectDevice(selected.id)}><Crosshair size={17} /></button>
              </div>
            </div>
            <div className="map-legend"><span><i className="legend-moving" /> Moving</span><span><i className="legend-parked" /> Parked</span><span><i className="legend-offline" /> Offline</span></div>
            <div className="focus-banner">
              <div className="focus-device" style={{ "--device-color": selected.color } as React.CSSProperties}><Navigation size={17} /></div>
              <div><span>{focused ? settings.demoMode ? "High-detail focus" : "Dashboard focus" : "Normal reporting"}</span><strong>{selected.nickname}</strong></div>
              <div className="focus-rate"><span>{settings.demoMode ? "Effective rate" : "Desired normal rate"}</span><strong>{focused && settings.demoMode ? "1 second" : secondsLabel(selected.pollRate)}</strong></div>
              <button onClick={() => focused ? setFocusedId("") : setFocusedId(selected.id)}>{focused ? "End focus" : "Focus live"}</button>
            </div>
          </div>

          <aside className="detail-panel panel">
            <div className="detail-head">
              <div className="detail-title"><div className="vehicle-avatar large" style={{ "--device-color": selected.color } as React.CSSProperties}><Car size={21} /></div><div><span className="device-id">{selected.id}</span><h2>{selected.nickname}</h2><p>{selected.vehicle} · {selected.plate}</p></div></div>
              <button className="quiet-icon-button" aria-label="Edit RoadLink" onClick={() => openEditor(selected)}><Edit3 size={16} /></button>
            </div>

            <div className="live-strip"><span className="live-pip" /><strong>{focused ? settings.demoMode ? "Focused live stream" : "Focused dashboard view" : "Normal stream"}</strong><span>Updated {timeAgo(selected.updatedAt, now)}</span></div>

            <div className="metrics-grid">
              <Metric icon={<Gauge size={16} />} label="Speed" value={displaySpeed(selected.speed)} detail={selected.status} />
              <Metric icon={<Activity size={16} />} label="Engine" value={selected.rpm ? selected.rpm.toLocaleString() : "—"} detail="rpm" />
              <Metric icon={<Thermometer size={16} />} label="Coolant" value={`${selected.coolant}°C`} detail="normal" />
              <Metric icon={<BatteryCharging size={16} />} label="Battery" value={`${selected.battery.toFixed(1)} V`} detail={selected.battery >= 12.4 ? "charging" : "low"} />
            </div>

            <div className="chart-section">
              <div className="chart-heading"><div><span>{chartLabel} · last 30 min</span><strong>{chartValue}</strong></div><div className="chart-change"><span>Live</span></div></div>
              <div className="chart-tabs">
                {(["speed", "rpm", "coolant"] as ChartMetric[]).map((metric) => <button key={metric} className={chartMetric === metric ? "is-active" : ""} onClick={() => setChartMetric(metric)}>{metric === "rpm" ? "RPM" : metric[0].toUpperCase() + metric.slice(1)}</button>)}
              </div>
              <TelemetryChart samples={selected.history} metric={chartMetric} color={selected.color} />
              <div className="chart-axis"><span>-30 min</span><span>-15 min</span><span>Now</span></div>
            </div>

            <div className="secondary-metrics">
              <div><Fuel size={15} /><span>Fuel</span><strong>{selected.fuel}%</strong></div>
              <div><Signal size={15} /><span>LTE signal</span><strong>{selected.signal ? `${selected.signal}/5` : "None"}</strong></div>
              <div><MapPin size={15} /><span>GPS</span><strong>{selected.status === "offline" ? "Last known" : "3 m fix"}</strong></div>
            </div>

            <div className="reporting-control">
              <div><span>Normal reporting rate</span><small>{settings.demoMode ? "Focus temporarily overrides this to 1 second." : "Saved locally; firmware command delivery comes later."}</small></div>
              <label><select value={selected.pollRate} onChange={(event) => updatePollRate(selected.id, Number(event.target.value))}>{pollOptions.map((option) => <option key={option} value={option}>{secondsLabel(option)}</option>)}</select><ChevronDown size={14} /></label>
            </div>

            <div className="device-meta">
              <div><span>IMEI</span><strong>{selected.imei}</strong></div>
              <div><span>Firmware</span><strong>{selected.firmware}</strong></div>
              <div><span>Odometer</span><strong>{selected.odometer.toLocaleString()} km</strong></div>
            </div>
          </aside>
          </> : (
            <div className="source-empty panel">
              <div className="source-empty-icon"><WifiOff size={28} /></div>
              <p className="eyebrow">Telemetry source</p>
              <h2>{receiverHealthy ? "Waiting for your first RoadLink" : "Start the telemetry receiver"}</h2>
              <p>Demo mode is fully disabled. Send an authenticated HTTP POST to the receiver and the RoadLink will appear here automatically.</p>
              <div className={`source-empty-status ${receiverHealthy ? "is-live" : ""}`}><span /><div><strong>{receiverHealthy ? "Listening for telemetry" : "Receiver is stopped"}</strong><small>{receiverState?.localEndpoint ?? "Loading receiver configuration…"}</small></div></div>
              <div className="source-empty-actions">
                <button className="primary-button" onClick={() => setShowReceiver(true)}><Network size={16} /> Receiver setup</button>
                <button className="secondary-button" onClick={toggleDemoMode}><Radio size={16} /> Enable demo mode</button>
              </div>
            </div>
          )}
        </section>
      </main>

      {editId && (
        <div className="modal-backdrop" onMouseDown={() => setEditId(null)}>
          <div className="modal" onMouseDown={(event) => event.stopPropagation()} role="dialog" aria-modal="true" aria-labelledby="edit-title">
            <div className="modal-head"><div><span className="modal-icon"><Edit3 size={18} /></span><div><h2 id="edit-title">Edit RoadLink</h2><p>{editId}</p></div></div><button className="quiet-icon-button" onClick={() => setEditId(null)}><X size={18} /></button></div>
            <div className="form-grid">
              <label className="field full"><span>Nickname</span><input autoFocus value={editDraft.nickname} onChange={(event) => setEditDraft((current) => ({ ...current, nickname: event.target.value }))} placeholder="Delivery Van 07" /></label>
              <label className="field full"><span>Vehicle</span><input value={editDraft.vehicle} onChange={(event) => setEditDraft((current) => ({ ...current, vehicle: event.target.value }))} placeholder="2021 Ford Transit" /></label>
              <label className="field"><span>License plate</span><input value={editDraft.plate} onChange={(event) => setEditDraft((current) => ({ ...current, plate: event.target.value }))} /></label>
              <label className="field"><span>Normal reporting</span><select value={editDraft.pollRate} onChange={(event) => setEditDraft((current) => ({ ...current, pollRate: Number(event.target.value) }))}>{pollOptions.map((option) => <option key={option} value={option}>{secondsLabel(option)}</option>)}</select></label>
            </div>
            <div className="info-note"><Zap size={16} /><span>{settings.demoMode ? "Selecting this RoadLink boosts its simulated rate to 1 second." : "Nickname and vehicle details are stored locally. Reporting commands require a future two-way firmware channel."}</span></div>
            <div className="modal-actions"><button className="secondary-button" onClick={() => setEditId(null)}>Cancel</button><button className="primary-button" onClick={saveEditor}><Check size={16} /> Save changes</button></div>
          </div>
        </div>
      )}

      {showAdd && (
        <div className="modal-backdrop" onMouseDown={() => setShowAdd(false)}>
          <div className="modal" onMouseDown={(event) => event.stopPropagation()} role="dialog" aria-modal="true" aria-labelledby="add-title">
            <div className="modal-head"><div><span className="modal-icon"><Plus size={18} /></span><div><h2 id="add-title">Add a RoadLink</h2><p>Create the unit now; pair LTE ingestion later.</p></div></div><button className="quiet-icon-button" onClick={() => setShowAdd(false)}><X size={18} /></button></div>
            <div className="form-grid">
              <label className="field"><span>Device ID</span><input autoFocus value={addDraft.id} onChange={(event) => setAddDraft((current) => ({ ...current, id: event.target.value }))} placeholder="RL-1050" /></label>
              <label className="field"><span>IMEI (optional)</span><input value={addDraft.imei} onChange={(event) => setAddDraft((current) => ({ ...current, imei: event.target.value }))} placeholder="867997..." /></label>
              <label className="field full"><span>Nickname</span><input value={addDraft.nickname} onChange={(event) => setAddDraft((current) => ({ ...current, nickname: event.target.value }))} placeholder="Field Truck 03" /></label>
              <label className="field"><span>Vehicle</span><input value={addDraft.vehicle} onChange={(event) => setAddDraft((current) => ({ ...current, vehicle: event.target.value }))} placeholder="2022 Nissan NP300" /></label>
              <label className="field"><span>Normal reporting</span><select value={addDraft.pollRate} onChange={(event) => setAddDraft((current) => ({ ...current, pollRate: Number(event.target.value) }))}>{pollOptions.map((option) => <option key={option} value={option}>{secondsLabel(option)}</option>)}</select></label>
            </div>
            <div className="info-note"><Radio size={16} /><span>This first version creates a simulated unit. The same record will later be claimed by the device IMEI through the ingest server.</span></div>
            <div className="modal-actions"><button className="secondary-button" onClick={() => setShowAdd(false)}>Cancel</button><button className="primary-button" onClick={addDevice}><Plus size={16} /> Add RoadLink</button></div>
          </div>
        </div>
      )}

      {showReceiver && (
        <div className="modal-backdrop" onMouseDown={() => setShowReceiver(false)}>
          <div className="modal receiver-modal" onMouseDown={(event) => event.stopPropagation()} role="dialog" aria-modal="true" aria-labelledby="receiver-title">
            <div className="modal-head">
              <div><span className="modal-icon"><Network size={18} /></span><div><h2 id="receiver-title">RoadLink telemetry receiver</h2><p>Compatible with the current firmware POST /telemetry protocol.</p></div></div>
              <button className="quiet-icon-button" onClick={() => setShowReceiver(false)}><X size={18} /></button>
            </div>

            {receiverState ? <>
              <div className={`receiver-hero ${receiverHealthy ? "is-live" : ""}`}>
                <div className="receiver-hero-icon"><Server size={22} /></div>
                <div><span>{receiverHealthy ? "LISTENING" : "STOPPED"}</span><strong>{receiverHealthy ? `All network interfaces · TCP ${receiverState.port}` : "Inbound telemetry is paused"}</strong><small>{receiverState.packetCount} authenticated packets received this run</small></div>
                <button disabled={receiverBusy} className={receiverHealthy ? "secondary-button" : "primary-button"} onClick={() => {
                  const receiver = window.roadlinkDesktop?.receiver;
                  if (!receiver) return;
                  void runReceiverAction(receiverHealthy ? () => receiver.stop() : () => receiver.start(), receiverHealthy ? "Telemetry receiver stopped" : "Telemetry receiver started");
                }}><Power size={15} /> {receiverHealthy ? "Stop" : "Start"}</button>
              </div>

              <div className="receiver-section">
                <div className="receiver-section-title"><div><span>Enter these values on your RoadLink</span><small>{receiverState.mapping ? `Public endpoint created with ${receiverState.mapping.method}` : "LAN endpoint; use public mapping for cellular access"}</small></div><ShieldCheck size={17} /></div>
                <div className="credential-grid">
                  <div><span>Receiver IP</span><strong>{receiverState.mapping?.publicIp ?? receiverState.lanAddresses[0]?.address ?? "127.0.0.1"}</strong><button onClick={() => copyValue(receiverState.mapping?.publicIp ?? receiverState.lanAddresses[0]?.address ?? "127.0.0.1", "Receiver IP")}><Copy size={13} /></button></div>
                  <div><span>TCP port</span><strong>{receiverState.mapping?.publicPort ?? receiverState.port}</strong><button onClick={() => copyValue(String(receiverState.mapping?.publicPort ?? receiverState.port), "TCP port")}><Copy size={13} /></button></div>
                  <div className="key-credential"><span>Six-digit access key</span><strong>{receiverState.accessKey}</strong><button onClick={() => copyValue(receiverState.accessKey, "Access key")}><Copy size={13} /></button></div>
                </div>
                <div className="endpoint-line"><Globe2 size={14} /><span>{receiverState.publicEndpoint ?? receiverState.localEndpoint}</span><button onClick={() => copyValue(receiverState.publicEndpoint ?? receiverState.localEndpoint, "Endpoint")}><Copy size={13} /> Copy</button></div>
              </div>

              <div className="receiver-settings-grid">
                <div className="receiver-setting-card">
                  <div className="receiver-setting-copy"><strong>Listening port</strong><span>Changing it restarts the local receiver.</span></div>
                  <div className="port-editor"><input inputMode="numeric" value={receiverPortDraft} onChange={(event) => setReceiverPortDraft(event.target.value.replace(/\D/g, "").slice(0, 5))} /><button disabled={receiverBusy || receiverPortDraft === String(receiverState.port)} onClick={applyReceiverPort}>Apply</button></div>
                </div>
                <div className="receiver-setting-card">
                  <div className="receiver-setting-copy"><strong>Automatic public port</strong><span>Tries NAT-PMP, then UPnP. May fail behind CGNAT.</span></div>
                  <button disabled={receiverBusy} className={`toggle ${receiverState.autoPortMap ? "is-on" : ""}`} onClick={() => {
                    const receiver = window.roadlinkDesktop?.receiver;
                    if (receiver) void runReceiverAction(() => receiver.update({ autoPortMap: !receiverState.autoPortMap, enabled: true }), receiverState.autoPortMap ? "Public port mapping disabled" : "Trying NAT-PMP and UPnP");
                  }} aria-label="Toggle automatic router port mapping"><span /></button>
                </div>
              </div>

              {receiverState.lastError && <div className="receiver-error"><AlertTriangle size={16} /><div><strong>Receiver notice</strong><span>{receiverState.lastError}</span></div></div>}

              <div className="receiver-actions-row">
                <button className="secondary-button" disabled={receiverBusy} onClick={addFirewallRule}><ShieldCheck size={15} /> Add Windows Firewall rule</button>
                <button className="secondary-button" onClick={() => window.roadlinkDesktop?.receiver.openLogFolder()}><FolderOpen size={15} /> Open telemetry logs</button>
                <button className="secondary-button danger-soft" disabled={receiverBusy} onClick={() => {
                  const receiver = window.roadlinkDesktop?.receiver;
                  if (receiver) void runReceiverAction(() => receiver.rotateKey(), "New access key generated; update every RoadLink");
                }}><RefreshCw size={15} /> Rotate key</button>
              </div>

              <div className="security-note"><KeyRound size={16} /><span>The six-digit key matches the current firmware and requests are rate-limited. Public exposure is opt-in; for a larger deployment, the next architecture should use HTTPS and a unique secret per RoadLink.</span></div>
            </> : <div className="receiver-loading"><RefreshCw size={20} /><span>Loading receiver service…</span></div>}

            <div className="modal-actions"><button className="primary-button" onClick={() => setShowReceiver(false)}>Done</button></div>
          </div>
        </div>
      )}

      {showSettings && (
        <div className="modal-backdrop" onMouseDown={() => setShowSettings(false)}>
          <div className="modal settings-modal" onMouseDown={(event) => event.stopPropagation()} role="dialog" aria-modal="true" aria-labelledby="settings-title">
            <div className="modal-head"><div><span className="modal-icon"><Settings size={18} /></span><div><h2 id="settings-title">Fleet preferences</h2><p>Local to this PC.</p></div></div><button className="quiet-icon-button" onClick={() => setShowSettings(false)}><X size={18} /></button></div>
            <div className="setting-row demo-setting"><div><strong>Demo mode</strong><span>Show simulated RoadLinks and generate local telemetry.</span></div><div className="setting-toggle-wrap"><b>{settings.demoMode ? "Enabled" : "Disabled"}</b><button className={`toggle ${settings.demoMode ? "is-on" : ""}`} onClick={toggleDemoMode} aria-label="Toggle demo mode"><span /></button></div></div>
            <div className="setting-row"><div><strong>Accent color</strong><span>Keep the interface recognizable at a glance.</span></div><div className="accent-options">{(["lime", "cyan", "amber"] as Accent[]).map((accent) => <button key={accent} className={`${accent} ${settings.accent === accent ? "is-selected" : ""}`} onClick={() => setSettings((current) => ({ ...current, accent }))} aria-label={`${accent} accent`}>{settings.accent === accent && <Check size={14} />}</button>)}</div></div>
            <div className="setting-row"><div><strong>Map appearance</strong><span>CARTO street maps or MapTiler satellite imagery.</span></div><div className="segmented map-segments"><button className={settings.mapStyle === "dark" ? "is-active" : ""} onClick={() => setSettings((current) => ({ ...current, mapStyle: "dark" }))}>Dark</button><button className={settings.mapStyle === "street" ? "is-active" : ""} onClick={() => setSettings((current) => ({ ...current, mapStyle: "street" }))}>Street</button><button disabled={!settings.mapTilerKey.trim()} title={settings.mapTilerKey.trim() ? "Use satellite imagery" : "Add a MapTiler key below first"} className={settings.mapStyle === "satellite" ? "is-active" : ""} onClick={() => setSettings((current) => ({ ...current, mapStyle: "satellite" }))}>Satellite</button></div></div>
            <div className="setting-row key-setting"><div><strong>MapTiler API key</strong><span>Required only for Satellite. Stored locally on this PC.</span></div><label className="settings-key-field"><input type="password" value={settings.mapTilerKey} onChange={(event) => { const mapTilerKey = event.target.value; setSettings((current) => ({ ...current, mapTilerKey, mapStyle: !mapTilerKey.trim() && current.mapStyle === "satellite" ? "dark" : current.mapStyle })); }} placeholder="Paste your MapTiler key" /><span>{settings.mapTilerKey.trim() ? <><Check size={12} /> Key saved</> : "No key"}</span></label></div>
            <div className="setting-row"><div><strong>Speed units</strong><span>Telemetry remains stored in metric units.</span></div><div className="segmented"><button className={settings.units === "kmh" ? "is-active" : ""} onClick={() => setSettings((current) => ({ ...current, units: "kmh" }))}>km/h</button><button className={settings.units === "mph" ? "is-active" : ""} onClick={() => setSettings((current) => ({ ...current, units: "mph" }))}>mph</button></div></div>
            <div className="setting-row"><div><strong>Compact device list</strong><span>Fit more RoadLinks on smaller displays.</span></div><button className={`toggle ${settings.compact ? "is-on" : ""}`} onClick={() => setSettings((current) => ({ ...current, compact: !current.compact }))}><span /></button></div>
            <div className="settings-footer"><button className="text-button" onClick={resetDemo}>Restore demo fleet</button><button className="primary-button" onClick={() => setShowSettings(false)}>Done</button></div>
          </div>
        </div>
      )}

      {toast && <div className="toast"><Check size={16} /><span>{toast}</span></div>}
    </div>
  );
}
