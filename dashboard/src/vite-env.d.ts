/// <reference types="vite/client" />

type ReceiverState = {
  running: boolean;
  status: "listening" | "stopped" | "disabled";
  port: number;
  accessKey: string;
  autoPortMap: boolean;
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
  packetCount: number;
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

interface Window {
  roadlinkDesktop?: {
    platform: string;
    version: string;
    copyText: (value: string) => void;
    receiver: {
      getState: () => Promise<ReceiverState>;
      start: () => Promise<ReceiverState>;
      stop: () => Promise<ReceiverState>;
      update: (patch: Partial<Pick<ReceiverState, "port" | "accessKey" | "autoPortMap">> & { enabled?: boolean }) => Promise<ReceiverState>;
      rotateKey: () => Promise<ReceiverState>;
      addFirewallRule: () => Promise<{ ok: boolean; ruleName: string }>;
      openLogFolder: () => Promise<void>;
      onState: (callback: (state: ReceiverState) => void) => () => void;
      onTelemetry: (callback: (telemetry: RoadLinkTelemetry) => void) => () => void;
    };
  };
}
