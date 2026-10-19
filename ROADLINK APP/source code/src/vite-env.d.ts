/// <reference types="vite/client" />

type ReceiverState = {
  running: boolean;
  status: "listening" | "stopped" | "disabled";
  port: number;
  accessKey: string;
  autoPortMap: boolean;
  freeTunnel: boolean;
  lanAddresses: Array<{ name: string; address: string }>;
  localEndpoint: string;
  loopbackEndpoint: string;
  publicEndpoint: string | null;
  mapping: null | {
    method: "NAT-PMP" | "UPnP";
    publicIp: string;
    publicPort: number;
    lifetimeSeconds: number;
  };
  tunnel: {
    status: "stopped" | "starting" | "active" | "error";
    publicHost: string | null;
    publicIp: string | null;
    publicPort: number | null;
    startedAt: string | null;
    expiresAt: string | null;
    lastError: string | null;
  };
  packetCount: number;
  heartbeatCount: number;
  streamingDevices: StreamingDevice[];
  lastPacketAt: string | null;
  lastError: string | null;
  logPath: string;
};

type RoadLinkTelemetry = {
  device?: string;
  device_id?: string;
  imei?: string;
  firmware?: string;
  sequence?: number;
  captured_at?: string;
  uptime_ms?: number;
  type?: string;
  config?: { running?: boolean; gps?: boolean; obd?: boolean; obd_fields?: number; interval_seconds?: number };
  gps?: {
    valid?: boolean;
    latitude?: number | null;
    longitude?: number | null;
    altitude_m?: number | null;
    speed_kmh?: number | null;
    course_deg?: number | null;
    heading_deg?: number | null;
    satellites?: number | null;
  };
  obd?: {
    rpm?: number | null;
    speed_kmh?: number | null;
    coolant_c?: number | null;
    throttle_pct?: number | null;
    map_kpa?: number | null;
    intake_c?: number | null;
    timing_deg?: number | null;
    voltage_v?: number | null;
    fuel_pct?: number | null;
    odometer_km?: number | null;
  };
  modem?: { signal_bars?: number; rssi?: number; technology?: string };
  _received_at: string;
  _client: string;
};

type TripSample = {
  timestamp: number;
  latitude: number | null;
  longitude: number | null;
  speed: number | null;
  rpm: number | null;
  coolant: number | null;
  voltage: number | null;
  fuel: number | null;
  throttle: number | null;
  satellites: number | null;
};

type TripSummary = { durationSeconds: number; distanceKm: number; topSpeedKmh: number; maxRpm: number; maxCoolantC: number; gpsPoints: number; sampleCount: number };
type TripRecord = { id: string; deviceId: string; nickname: string; plate: string; vehicle: string; startedAt: number; endedAt: number; summary: TripSummary };
type ActiveTrip = Omit<TripRecord, "endedAt">;

interface Window {
  roadlinkDesktop?: {
    platform: string;
    version: string;
    copyText: (value: string) => void;
    receiver: {
      getState: () => Promise<ReceiverState>;
      configureStreaming: (id: string, patch: Partial<Omit<StreamingConfig, "revision">>, revision: number) => Promise<ReceiverState>;
      start: () => Promise<ReceiverState>;
      stop: () => Promise<ReceiverState>;
      update: (patch: Partial<Pick<ReceiverState, "port" | "accessKey" | "autoPortMap" | "freeTunnel">> & { enabled?: boolean }) => Promise<ReceiverState>;
      rotateKey: () => Promise<ReceiverState>;
      addFirewallRule: () => Promise<{ ok: boolean; ruleName: string }>;
      openLogFolder: () => Promise<void>;
      onState: (callback: (state: ReceiverState) => void) => () => void;
      onTelemetry: (callback: (telemetry: RoadLinkTelemetry) => void) => () => void;
      onHeartbeat: (callback: (heartbeat: RoadLinkTelemetry) => void) => () => void;
    };
    trips: {
      list: () => Promise<TripRecord[]>;
      active: () => Promise<ActiveTrip | null>;
      start: (details: { deviceId: string; nickname: string; plate: string; vehicle: string }) => Promise<ActiveTrip>;
      stop: () => Promise<TripRecord>;
      get: (id: string) => Promise<TripRecord & { samples: TripSample[] }>;
      openFolder: () => Promise<string>;
      onChange: (callback: (trip: ActiveTrip | null) => void) => () => void;
      onError: (callback: (message: string) => void) => () => void;
    };
  };
}

type StreamingConfig = {
  revision: number;
  running: boolean;
  gps: boolean;
  obd: boolean;
  obd_fields: number;
  interval_seconds: number;
};
type StreamingDevice = {
  id: string;
  stableIdentity: boolean;
  applied: StreamingConfig;
  desired: StreamingConfig | null;
  status: "applied" | "pending" | "changed-on-device" | "conflict";
  lastSeenAt: string;
  lastHeartbeatAt: string | null;
};
