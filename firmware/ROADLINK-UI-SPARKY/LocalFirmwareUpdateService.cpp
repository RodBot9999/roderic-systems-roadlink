#include "LocalFirmwareUpdateService.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

namespace {
constexpr char kCredentialNamespace[] = "roadlink_ota";
constexpr char kPasswordKey[] = "password";
constexpr char kPasswordAlphabet[] =
    "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
constexpr size_t kPasswordLength = 20;
}  // namespace

String LocalFirmwareUpdateService::loadOrCreatePassword() {
  Preferences preferences;
  if (!preferences.begin(kCredentialNamespace, false)) return "";

  String password = preferences.getString(kPasswordKey, "");
  if (password.length() != kPasswordLength) {
    password = "";
    password.reserve(kPasswordLength);
    constexpr size_t alphabetLength = sizeof(kPasswordAlphabet) - 1;
    for (size_t index = 0; index < kPasswordLength; ++index) {
      password += kPasswordAlphabet[esp_random() % alphabetLength];
    }
    if (preferences.putString(kPasswordKey, password) != kPasswordLength) {
      password = "";
    }
  }
  preferences.end();
  return password;
}

void LocalFirmwareUpdateService::begin() {
  if (active_) return;
  if (!esp_ota_get_next_update_partition(nullptr)) {
    Serial.println(F("[OTA] No inactive app partition; select an OTA partition scheme"));
    return;
  }

  // Enable the radio before generating a new credential so esp_random has
  // the Wi-Fi entropy source available on first boot.
  if (!WiFi.mode(WIFI_AP)) {
    Serial.println(F("[OTA] Could not start Wi-Fi AP mode"));
    return;
  }
  const String password = loadOrCreatePassword();
  if (password.length() != kPasswordLength) {
    Serial.println(F("[OTA] Could not store the local update password"));
    WiFi.mode(WIFI_OFF);
    return;
  }

  char ssid[24];
  snprintf(ssid, sizeof(ssid), "RoadLink-OTA-%06lX",
           static_cast<unsigned long>(ESP.getEfuseMac() & 0xFFFFFFULL));
  if (!WiFi.softAP(ssid, password.c_str())) {
    Serial.println(F("[OTA] Could not create the local update access point"));
    WiFi.mode(WIFI_OFF);
    return;
  }

  // HTTPUpdateServer handles authenticated upload and writes directly to the
  // inactive OTA slot. No filesystem or full-image RAM buffer is required.
  if (!routesConfigured_) {
    updater_.setup(&server_, "/update", "roadlink", password);
    routesConfigured_ = true;
  }
  server_.begin();
  active_ = true;

  Serial.printf("[OTA] Connect to Wi-Fi %s with password %s\n", ssid,
                password.c_str());
  Serial.println(F("[OTA] Open http://192.168.4.1/update"));
  Serial.println(F("[OTA] Browser login: roadlink / same Wi-Fi password"));
}

void LocalFirmwareUpdateService::update() {
  if (active_) server_.handleClient();
}

void LocalFirmwareUpdateService::setEnabled(bool enabled) {
  if (requestedEnabled_ == enabled) return;
  requestedEnabled_ = enabled;
  if (enabled) begin();
  else stop();
}

void LocalFirmwareUpdateService::stop() {
  if (!active_) return;
  server_.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  active_ = false;
  Serial.println(F("[OTA] Local Wi-Fi update access point disabled"));
}

bool LocalFirmwareUpdateService::active() const {
  return active_;
}
