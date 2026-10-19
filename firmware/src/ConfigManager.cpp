#include "ConfigManager.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_random.h>

#include "NetCheck.h"
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
    cfg.ipAddr = wlan["ip"] | cfg.ipAddr;
    cfg.ipMask = wlan["mask"] | cfg.ipMask;
    cfg.ipGw = wlan["gw"] | cfg.ipGw;
    cfg.ipDns1 = wlan["dns1"] | cfg.ipDns1;
    cfg.ipDns2 = wlan["dns2"] | cfg.ipDns2;
    cfg.ipPendingCheck = wlan["ipCheck"] | cfg.ipPendingCheck;
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
  wlan["ip"] = _config.ipAddr;
  wlan["mask"] = _config.ipMask;
  wlan["gw"] = _config.ipGw;
  wlan["dns1"] = _config.ipDns1;
  wlan["dns2"] = _config.ipDns2;
  wlan["ipCheck"] = _config.ipPendingCheck;
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
  // Feste IP aus einem Backup immer neu pruefen (Geraet kann woanders stehen).
  String ipErr;
  if (cfg.staticIp() && !applyIp(cfg, cfg.ipAddr, cfg.ipMask, cfg.ipGw, cfg.ipDns1, cfg.ipDns2, ipErr)) {
    error = "feste IP im Backup ungueltig: " + ipErr;
    return false;
  }
  if (cfg.staticIp()) cfg.ipPendingCheck = true;
  _config = cfg;
  return save();
}

static bool parseIp(const String& in, IPAddress& out) {
  String t = in;
  t.trim();
  return t.length() > 0 && out.fromString(t);
}

// wie GatewayConfig::applyIp im esp-meshcore-gateway (E40)
bool ConfigManager::applyIp(DeviceConfig& s, const String& ipIn, const String& maskIn, const String& gwIn,
                            const String& dns1In, const String& dns2In, String& err) {
  String ipT = ipIn;
  ipT.trim();
  if (ipT.isEmpty()) {  // DHCP
    s.ipAddr = s.ipMask = s.ipGw = s.ipDns1 = s.ipDns2 = String();
    s.ipPendingCheck = false;
    return true;
  }
  IPAddress ip, mask, gw, dns1, dns2;
  if (!parseIp(ipT, ip)) { err = "IP-Adresse ungueltig"; return false; }
  if (!parseIp(maskIn, mask) || !net_check::validMask(mask)) { err = "Subnetzmaske ungueltig (z. B. 255.255.255.0)"; return false; }
  if (!parseIp(gwIn, gw)) { err = "Gateway (Router) ungueltig"; return false; }
  String d1 = dns1In, d2 = dns2In;
  d1.trim();
  d2.trim();
  if (d1.length() && !parseIp(d1, dns1)) { err = "DNS 1 ungueltig"; return false; }
  if (d2.length() && !parseIp(d2, dns2)) { err = "DNS 2 ungueltig"; return false; }
  uint32_t m = (uint32_t)mask, a = (uint32_t)ip, g = (uint32_t)gw;  // Netzreihenfolge, Maske wirkt byteweise gleich
  if ((a & m) != (g & m)) { err = "Gateway liegt nicht im Subnetz der IP-Adresse"; return false; }
  if ((a & ~m) == 0 || (a & ~m) == ~m) { err = "IP-Adresse ist Netz- oder Broadcast-Adresse"; return false; }
  if (a == g) { err = "IP-Adresse und Gateway sind gleich"; return false; }
  String ipStr = ip.toString();
  if (ipStr != s.ipAddr) s.ipPendingCheck = true;  // neue Adresse -> vor dem Umschalten pruefen
  s.ipAddr = ipStr;
  s.ipMask = mask.toString();
  s.ipGw = gw.toString();
  s.ipDns1 = d1.length() ? dns1.toString() : String();
  s.ipDns2 = d2.length() ? dns2.toString() : String();
  return true;
}

void ConfigManager::setConfig(const DeviceConfig& config) {
  _config = config;
  save();
}
