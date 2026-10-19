#include "ConfigManager.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_random.h>

#include "version.h"

namespace {
constexpr const char* kConfigFile = "/config.json";
constexpr const char* kConfigFileTmp = "/config.json.tmp";
}  // namespace

String ConfigManager::randomKey() {
  char buf[33];
  for (int i = 0; i < 16; i++) snprintf(buf + 2 * i, 3, "%02x", (unsigned)(esp_random() & 0xFF));
  return String(buf);
}

void ConfigManager::begin() {
  if (!load()) {
    Serial.println("[CONFIG] config.json fehlt/ungueltig -> Standardwerte werden gespeichert");
    _config = DeviceConfig();
  }
  if (_config.apiKey.length() < 16) {
    _config.apiKey = randomKey();
    save();
  }
}

bool ConfigManager::parse(const String& json, DeviceConfig& cfg, String& error) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    error = String("kein gueltiges JSON (") + err.c_str() + ")";
    return false;
  }
  const char* project = doc["project"] | "";
  if (project[0] && strcmp(project, FIRMWARE_PROJECT_ID) != 0) {
    error = String("Backup stammt von einem anderen Projekt (") + project + ")";
    return false;
  }
  if (!doc["systemName"].is<const char*>()) {
    error = "kein ESP-P4d-Einstellungs-Backup (systemName fehlt)";
    return false;
  }
  cfg = DeviceConfig();
  cfg.systemName = doc["systemName"] | cfg.systemName;
  cfg.settingsPassword = doc["settingsPassword"] | cfg.settingsPassword;
  cfg.apiKey = doc["apiKey"] | cfg.apiKey;
  cfg.ntpServer = doc["ntpServer"] | cfg.ntpServer;
  for (JsonVariant v : doc["summary"].as<JsonArray>()) {
    const char* s = v | "";
    if (s[0]) cfg.summary.push_back((uint16_t)strtoul(s, nullptr, 16));
  }
  JsonObject wlan = doc["wlan"];
  if (wlan) {
    cfg.wlanSsid = wlan["ssid"] | cfg.wlanSsid;
    cfg.wlanPsk = wlan["psk"] | cfg.wlanPsk;
  }
  if (cfg.systemName.isEmpty() || cfg.settingsPassword.isEmpty()) {
    error = "Geraetename oder Passwort leer";
    return false;
  }
  return true;
}

bool ConfigManager::load() {
  fs::File f = LittleFS.open(kConfigFile, "r");
  if (!f) return false;
  String json = f.readString();
  f.close();
  String error;
  if (!parse(json, _config, error)) {
    Serial.printf("[CONFIG] %s\n", error.c_str());
    return false;
  }
  return true;
}

String ConfigManager::toJson() const {
  JsonDocument doc;
  doc["project"] = FIRMWARE_PROJECT_ID;
  doc["firmware"] = DEVICE_FIRMWARE_VERSION;
  doc["systemName"] = _config.systemName;
  doc["settingsPassword"] = _config.settingsPassword;
  doc["apiKey"] = _config.apiKey;
  doc["ntpServer"] = _config.ntpServer;
  JsonArray sum = doc["summary"].to<JsonArray>();
  for (uint16_t a : _config.summary) {
    char k[8];
    snprintf(k, sizeof(k), "0x%04X", a);
    sum.add(k);
  }
  JsonObject wlan = doc["wlan"].to<JsonObject>();
  wlan["ssid"] = _config.wlanSsid;
  wlan["psk"] = _config.wlanPsk;
  String out;
  serializeJsonPretty(doc, out);
  return out;
}

bool ConfigManager::save() {
  // Erst in eine tmp-Datei schreiben und dann umbenennen - ein Stromausfall
  // waehrend des Schreibens darf keine halbfertige config.json hinterlassen.
  fs::File f = LittleFS.open(kConfigFileTmp, "w");
  if (!f) {
    Serial.println("[CONFIG] config.json.tmp konnte nicht geschrieben werden");
    return false;
  }
  f.print(toJson());
  f.close();

  LittleFS.remove(kConfigFile);
  return LittleFS.rename(kConfigFileTmp, kConfigFile);
}

bool ConfigManager::restoreJson(const String& json, String& error) {
  DeviceConfig cfg;
  if (!parse(json, cfg, error)) return false;
  if (cfg.apiKey.length() < 16) cfg.apiKey = _config.apiKey;
  _config = cfg;
  return save();
}

void ConfigManager::setConfig(const DeviceConfig& config) {
  _config = config;
  save();
}
