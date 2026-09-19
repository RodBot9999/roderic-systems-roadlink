// Appended to a generated translation unit containing actual firmware headers
// and A7670Service.cpp. Tests use a fake UART rather than a second state machine.
#include <cassert>
#include <iostream>
#include <functional>

struct Fixture {
  GpsService gps;
  ObdService obd;
  A7670Service modem{gps, obd, 1};
  std::vector<std::string> writes;
  bool answer = true;
  int registration = 1;
  int radio = 1;
  int attached = 1;
  int active = 1;
  int operatorMode = 0;
  bool operatorAutoFails = false;
  bool resetAnswers = true;
  std::string httpBody;

  Fixture(bool endpoint = true) {
    fakeMillis = 0;
    HardwareSerial::input.clear();
    HardwareSerial::output.clear();
    uint8_t ip[4] = {203, 0, 113, 9};
    if (!endpoint) memset(ip, 0, sizeof(ip));
    modem.begin(115200, 17, 16, true, true, true, 10000,
        ip, endpoint ? 8080 : 0, 123456);
  }
  void respond(const std::string& output) {
    if (!answer || output.empty()) return;
    std::string reply = "\r\nOK\r\n";
    if (output.find("AT+CPIN?") == 0) reply = "\r\n+CPIN: READY\r\nOK\r\n";
    else if (output.find("AT+CFUN?") == 0)
      reply = "\r\n+CFUN: " + std::to_string(radio) + "\r\nOK\r\n";
    else if (output.find("AT+CFUN=1") == 0) radio = 1;
    else if (output.find("AT+COPS?") == 0)
      reply = "\r\n+COPS: " + std::to_string(operatorMode) + ",0,\"Carrier\",7\r\nOK\r\n";
    else if (output.find("AT+COPS=0") == 0 && operatorAutoFails) reply = "\r\nERROR\r\n";
    else if (output.find("AT+CEREG?") == 0)
      reply = "\r\n+CEREG: 0," + std::to_string(registration) + "\r\nOK\r\n";
    else if (output.find("AT+CSQ") == 0) reply = "\r\n+CSQ: 20,99\r\nOK\r\n";
    else if (output.find("AT+CGATT?") == 0)
      reply = "\r\n+CGATT: " + std::to_string(attached) + "\r\nOK\r\n";
    else if (output.find("AT+CGATT=1") == 0) attached = 1;
    else if (output.find("AT+CGACT?") == 0)
      reply = "\r\n+CGACT: 1," + std::to_string(active) + "\r\nOK\r\n";
    else if (output.find("AT+CGDCONT?") == 0)
      reply = "\r\n+CGDCONT: 1,\"IP\",\"carrier.apn\",\"10.1.2.3\"\r\nOK\r\n";
    else if (output.find("AT+CGPADDR") == 0)
      reply = active ? "\r\n+CGPADDR: 1,10.1.2.3\r\nOK\r\n"
                     : "\r\n+CGPADDR: 1,0.0.0.0\r\nOK\r\n";
    else if (output.find("AT+HTTPDATA=") == 0) reply = "\r\nDOWNLOAD\r\n";
    else if (output.find("AT+HTTPACTION=1") == 0)
      reply = "\r\nOK\r\n+HTTPACTION: 1,200," + std::to_string(httpBody.size()) + "\r\n";
    else if (output.find("AT+HTTPREAD=0,") == 0)
      reply = "\r\nOK\r\n+HTTPREAD: " + std::to_string(httpBody.size()) + "\r\n" + httpBody + "\r\n+HTTPREAD: 0\r\n";
    else if (output.find("AT+HTTPREAD") == 0) reply = "\r\nERROR\r\n";
    else if (output.find("AT+CRESET") == 0 && !resetAnswers) return;
    HardwareSerial::reply(reply);
  }
  void tick(uint32_t delta = 100) {
    fakeMillis += delta;
    modem.update();
    std::string output = std::move(HardwareSerial::output);
    HardwareSerial::output.clear();
    if (!output.empty()) { writes.push_back(output); respond(output); }
  }
  void waitFor(const std::function<bool()>& condition, uint32_t budget = 30000) {
    uint32_t start = fakeMillis;
    while (!condition() && fakeMillis - start < budget) tick();
    assert(condition());
  }
  void ready() { waitFor([&] { return modem.ready(); }); }
  void run(uint32_t duration) {
    uint32_t start = fakeMillis;
    while (fakeMillis - start < duration) tick();
  }
  int count(const std::string& prefix) const {
    int result = 0;
    for (const auto& output : writes) if (output.find(prefix) == 0) ++result;
    return result;
  }
  int containing(const std::string& value) const {
    int result = 0;
    for (const auto& output : writes) if (output.find(value) != std::string::npos) ++result;
    return result;
  }
  void noHttp() const { assert(count("AT+HTTP") == 0); }
  void noTelemetry() const { assert(containing("/telemetry") == 0); }
};

int main() {
  std::cout.setf(std::ios::unitbuf);
  assert(ModemReply::registration("\r\n+CEREG: 0,1\r\nOK\r\n+CEREG: 2\r\n") == 1);
  assert(ModemReply::registration("+CEREG: 2,\"ABCD\",\"1234\",7\r\n") == -1);
  assert(ModemReply::registration("+CEREG: 2,5\r\n+CEREG: 0,1\r\n") == 1);
  assert(ModemReply::registration("+CEREG: 0,") == -1);
  assert(ModemReply::registration("+CEREG: 0,3") == -1);
  assert(ModemReply::pair("+CGACT: 2,1\r\n+CGACT: 1,0\r\n", "+CGACT:", 1) == 0);
  assert(ModemReply::pair("+HTTPACTION: 1,20", "+HTTPACTION:", 1) == -1);
  assert(ModemReply::scalar("+CFUN: 4\r\n", "+CFUN:") == 4);
  assert(ModemReply::first("+COPS: 2\r\n", "+COPS:") == 2);
  assert(ModemReply::first("+COPS: 0,0,\"Carrier\",7\r\n", "+COPS:") == 0);
  { // Boot sends independent heartbeats but never starts telemetry on its own.
    std::cout << "case boot heartbeat\n";
    Fixture f; f.ready(); f.run(65000); f.noTelemetry();
    assert(!f.modem.telemetryRunning());
    assert(f.containing("/heartbeat") >= 2);
    assert(f.modem.snapshot().successfulHeartbeats >= 2);
    assert(f.count("AT+CEREG?") >= 3);
    assert(f.count("AT+CGACT=1,1") == 0);
    assert(f.count("AT+CGATT=1") == 0);
    assert(f.count("AT+CGDCONT=") == 0);
    assert(f.modem.snapshot().apn == "carrier.apn");
  }
  { // Warm disable/re-enable reuses the already registered, active bearer.
    std::cout << "case warm enable\n";
    Fixture f; f.ready();
    f.modem.setEnabled(false); f.tick();
    assert(f.modem.state() == A7670State::Disabled);
    f.modem.setEnabled(true); f.ready(); f.noTelemetry();
    assert(!f.modem.telemetryRunning());
    assert(f.count("AT+CEREG?") == 2);
    assert(f.count("AT+CGACT=1,1") == 0);
    assert(f.count("AT+CGATT=1") == 0);
  }
  { // START validates endpoint; inactive default bearer is not forced active.
    std::cout << "case start endpoint\n";
    Fixture f(false); f.active = 0; f.ready();
    assert(!f.modem.startTelemetry()); f.run(15000); f.noHttp();
    assert(f.count("AT+CGACT=1,1") == 0);
    uint8_t ip[4] = {203, 0, 113, 9}; f.modem.setEndpoint(ip, 8080, 123456);
    assert(f.modem.startTelemetry());
    f.waitFor([&] { return f.modem.snapshot().successfulPosts == 1; });
    f.modem.stopTelemetry(); f.ready();
    int telemetryUrls = f.containing("/telemetry"); f.run(30000);
    assert(f.containing("/telemetry") == telemetryUrls);
    assert(f.containing("/heartbeat") >= 1);
  }
  { // STOP in HTTPDATA input finishes local data, but never launches HTTPACTION.
    std::cout << "case stop data\n";
    Fixture f; f.ready(); assert(f.modem.startTelemetry());
    f.waitFor([&] { return f.count("AT+HTTPDATA=") == 1; });
    f.modem.stopTelemetry(); f.ready(); f.run(20000);
    assert(f.modem.snapshot().successfulPosts == 0);
    assert(f.count("AT+HTTPTERM") >= 2);
  }
  { // Disable during a pending status query waits for its reply before re-sync.
    std::cout << "case pending status\n";
    Fixture f; f.ready();
    f.waitFor([&] { return f.modem.snapshot().successfulHeartbeats == 1; });
    f.ready();
    f.answer = false;
    f.waitFor([&] { return f.modem.state() == A7670State::RadioQuery; }, 35000);
    f.tick(); int queries = f.count("AT+CFUN?");
    f.modem.setEnabled(false); f.tick(); assert(f.modem.state() != A7670State::Disabled);
    f.modem.setEnabled(true); f.tick(); assert(f.count("AT+CFUN?") == queries);
    HardwareSerial::reply("\r\n+CFUN: 1\r\nOK\r\n"); f.answer = true; f.ready();
    f.noTelemetry();
  }
  { // Search is not a failure; operator recovery is bounded to one per outage.
    std::cout << "case search\n";
    Fixture f; f.registration = 2; f.run(60000); f.noHttp();
    assert(f.modem.snapshot().failureCount == 0);
    f.run(320000);
    assert(f.count("AT+COPS=0") == 1);
    f.registration = 5; f.ready();
    assert(f.modem.snapshot().registrationStatus == 5);
  }
  { // Radio-off / detached states receive recovery only when needed.
    std::cout << "case radio recovery\n";
    Fixture f; f.radio = 4; f.attached = 0; f.ready(); f.noHttp();
    assert(f.count("AT+CFUN=1") == 1);
    assert(f.count("AT+CGATT=1") == 1);
  }
  { // Failed operator recovery must not repeat on a still-deregistered modem.
    std::cout << "case operator recovery\n";
    Fixture f; f.registration = 2; f.operatorMode = 2; f.operatorAutoFails = true;
    f.run(320000); f.noHttp(); assert(f.count("AT+COPS=0") == 1);
  }
  { // A fragmented HTTPACTION reply is not consumed before its complete line.
    std::cout << "case fragmented action\n";
    Fixture f; f.ready(); assert(f.modem.startTelemetry());
    f.waitFor([&] { return f.modem.state() == A7670State::HttpAction; });
    f.answer = false; f.tick();
    HardwareSerial::reply("\r\nOK\r\n+HTTPACTION: 1,20");
    f.tick(); assert(f.modem.snapshot().successfulPosts == 0);
    HardwareSerial::reply("0,"); f.tick();
    assert(f.modem.snapshot().successfulPosts == 0);
    HardwareSerial::reply("0\r\n"); f.tick();
    assert(f.modem.snapshot().successfulPosts == 1);
    f.answer = true; f.modem.stopTelemetry(); f.ready(); f.run(20000);
    assert(f.containing("/telemetry") == 1);
  }
  { // A heartbeat response yields one complete semantic configuration.
    std::cout << "case heartbeat config\n";
    Fixture f;
    f.httpBody = "{\"ok\":true,\"config\":{\"revision\":9,\"running\":true,\"gps\":false,\"obd\":true,\"obd_fields\":133,\"interval_seconds\":42}}";
    f.ready();
    f.waitFor([&] { return f.modem.state() == A7670State::HttpRead; });
    f.answer = false; f.tick();
    HardwareSerial::reply("\r\nOK\r\n"); f.tick();
    assert(f.modem.snapshot().successfulHeartbeats == 0);
    HardwareSerial::reply("+HTTPREAD: " + std::to_string(f.httpBody.size()) + "\r\n" + f.httpBody.substr(0, 20));
    f.tick(); assert(f.modem.snapshot().successfulHeartbeats == 0);
    HardwareSerial::reply(f.httpBody.substr(20) + "\r\n+HTTPREAD: 0\r\n");
    f.answer = true;
    f.waitFor([&] { return f.modem.snapshot().successfulHeartbeats == 1; });
    RemoteStreamingConfig config;
    assert(f.modem.takeRemoteConfig(config));
    assert(config.valid && config.revision == 9 && config.running);
    assert(!config.sendGps && config.sendObd);
    assert(config.obdFieldMask == 133 && config.intervalSeconds == 42);
    assert(!f.modem.takeRemoteConfig(config));
  }
  { // GPS numeric strings are normalized so leading zeroes never break JSON.
    std::cout << "case GPS JSON\n";
    Fixture f;
    f.gps.data.positionValid = false;
    f.gps.data.satellites = "00";
    f.gps.data.altitudeMeters = "001.20";
    f.obd.mask = StreamField::RPM;
    f.ready(); assert(f.modem.startTelemetry());
    f.waitFor([&] { return f.modem.snapshot().successfulPosts == 1; });
    assert(f.modem.lastPayload().indexOf("\"satellites\":0") >= 0);
    assert(f.modem.lastPayload().indexOf("\"altitude_m\":null") >= 0);
    assert(f.modem.lastPayload().indexOf("\"rpm\":") >= 0);
    assert(f.modem.lastPayload().indexOf("\"coolant_c\":") < 0);
    f.gps.data.positionValid = true;
    f.gps.data.latitudeDecimal = 19.4326;
    f.gps.data.longitudeDecimal = -99.1332;
    f.waitFor([&] { return f.modem.snapshot().successfulPosts == 2; });
    assert(f.modem.lastPayload().indexOf("\"valid\":true") >= 0);
    assert(f.modem.lastPayload().indexOf("\"altitude_m\":1.2") >= 0);
  }
  { // Short telemetry intervals must not starve independent heartbeats.
    Fixture f; f.ready(); f.modem.setSendInterval(1000);
    assert(f.modem.startTelemetry()); f.run(95000);
    assert(f.modem.snapshot().successfulPosts > 10);
    assert(f.modem.snapshot().successfulHeartbeats >= 3);
  }
  { // Denial is retained and includes actual registration status.
    std::cout << "case denial\n";
    Fixture f; f.registration = 3; f.run(10000);
    assert(f.modem.snapshot().failureCount == 1);
    assert(f.modem.snapshot().lastFailureType == "LTE REGISTRATION");
    f.registration = 1; f.ready();
    assert(f.modem.snapshot().failureCount == 1);
  }
  { // Reset still works with firmware modem checks disabled.
    std::cout << "case reset disabled\n";
    Fixture f; f.ready(); f.modem.setEnabled(false); f.tick();
    f.modem.requestSoftwareRestart();
    f.waitFor([&] { return f.modem.softwareRestartFinished(); });
    assert(f.modem.softwareRestartSucceeded());
    assert(f.count("AT+CRESET") == 1);
    assert(!f.modem.telemetryRunning());
  }
  { // No response to reset is bounded and reported as unconfirmed.
    std::cout << "case reset timeout\n";
    Fixture f; f.ready(); f.resetAnswers = false; f.modem.requestSoftwareRestart();
    f.waitFor([&] { return f.modem.softwareRestartFinished(); });
    assert(!f.modem.softwareRestartSucceeded());
  }
  { // Reboot during HTTPDATA settles local input before the UART reset.
    std::cout << "case reboot during data\n";
    Fixture f; f.ready(); assert(f.modem.startTelemetry());
    f.waitFor([&] { return f.count("AT+HTTPDATA=") == 1; });
    f.modem.requestSoftwareRestart();
    f.waitFor([&] { return f.modem.softwareRestartFinished(); });
    assert(f.modem.softwareRestartSucceeded());
    assert(f.count("AT+HTTPACTION=1") == 0);
    assert(f.count("AT+CRESET") == 1);
  }
  std::cout << "PASS: AT parser and 16 modem lifecycle regression scenarios\n";
}
