#include <cassert>
#include <cmath>
#include <iostream>

void feedGps(GpsService& gps, const std::string& body) {
  uint8_t checksum = 0;
  for (char c : body) checksum ^= c;
  char suffix[8]; snprintf(suffix, sizeof(suffix), "*%02X\r\n", checksum);
  HardwareSerial::reply("$" + body + suffix);
  gps.update();
}

int main() {
  fakeMillis = 100;
  GpsService gps(2); gps.begin(9600, 1, 2, 3);
  feedGps(gps, "GPGGA,120000,,,,,0,00,99.9,,,,,,");
  assert(!gps.hasFix() && gps.statistics().validChecksumCount == 1);
  feedGps(gps, "GPRMC,120001,A,1930.000,N,09900.000,W,01.2,090.0,180926,,");
  assert(gps.hasFix());
  assert(std::abs(gps.snapshot().latitudeDecimal - 19.5) < 0.00001);
  fakeMillis += AppConfig::GPS_FIX_STALE_MS + 1;
  assert(!gps.hasFix());
  feedGps(gps, "GNGGA,120012,1930.000,N,09900.000,W,1,08,1.0,001.2,M,0,M,,");
  assert(gps.hasFix());
  feedGps(gps, "GNRMC,120013,V,,,,,,,180926,,");
  assert(!gps.hasFix());
  feedGps(gps, "GPRMC,120014,A,1930.000,N,09900.000,W,01.2,090.0,180926,,");
  assert(gps.hasFix());

  CanService can;
  ObdService obd(can); obd.begin(100);
  A7670Service modem;
  AppSettings settings;
  settings.simObdFieldMask = StreamField::RPM | StreamField::COOLANT;
  SettingsStore store;
  StreamingController stream(can, obd, modem, settings, store);
  stream.begin();
  assert(!stream.running() && !obd.livePollingEnabled());
  assert(stream.start());
  assert(can.canTransmit() && obd.livePollingEnabled());
  assert(store.saves == 1);
  for (int i = 0; i < 20; ++i) {
    fakeMillis += 1000; obd.update(); stream.update();
  }
  assert(can.sent.size() > 2);
  bool rpm = false, coolant = false;
  for (const auto& frame : can.sent) {
    assert(frame.data[1] == 1);
    assert(frame.data[2] == 0x0C || frame.data[2] == 0x05);
    rpm |= frame.data[2] == 0x0C;
    coolant |= frame.data[2] == 0x05;
  }
  assert(rpm && coolant);
  assert(obd.startReadDtcs());
  assert(!obd.livePollingEnabled());
  fakeMillis += 10000; obd.update(); stream.update();
  assert(obd.livePollingEnabled());

  stream.setObdEnabled(false); stream.update();
  assert(!obd.livePollingEnabled() && stream.running());
  stream.setObdEnabled(true); stream.update();
  assert(obd.livePollingEnabled());
  stream.setIntervalMs(359999000UL);
  assert(store.saved.simSendIntervalMs == 359999000UL);
  stream.setIntervalMs(0);
  assert(store.saved.simSendIntervalMs == AppConfig::SIM_SEND_INTERVAL_MIN_MS);

  RemoteStreamingConfig remote;
  remote.valid = true; remote.revision = settings.streamConfigRevision + 1;
  remote.running = false; remote.sendGps = false; remote.sendObd = true;
  remote.obdFieldMask = StreamField::VOLTAGE; remote.intervalSeconds = 42;
  modem.pending = remote; stream.update();
  assert(!stream.running() && !obd.livePollingEnabled());
  assert(settings.simObdFieldMask == StreamField::VOLTAGE);
  assert(!settings.simSendGps && store.saved.simSendIntervalMs == 42000);
  const int saves = store.saves, syncs = modem.syncs;
  modem.pending = remote; stream.update();
  assert(store.saves == saves && modem.syncs == syncs);
  --remote.revision; remote.running = true;
  modem.pending = remote; stream.update();
  assert(!stream.running());
  remote.revision = settings.streamConfigRevision + 1;
  modem.available = false; modem.pending = remote; stream.update();
  assert(!stream.running());
  modem.available = true; fakeMillis += 5001; stream.update();
  assert(stream.running() && obd.livePollingEnabled());
  stream.stop();
  assert(!stream.running() && !obd.livePollingEnabled());
  std::cout << "PASS: GPS acquisition/loss/recovery, selected OBD polling, diagnostic resume, persistence and remote configuration\n";
}
