#include "NetworkManager.h"

#include <ESPmDNS.h>

#include "Log.h"

namespace {
// Wie in der sensormeter-Familie/ESP-Anwesenheit: eigener Access-Point mit festem
// Namen/Passwort unter 192.168.4.1, erst nach 5 min ohne WLAN (ein kurzer
// Router-Neustart soll ihn nicht ausloesen).
constexpr const char* kApSsid = "esp-p4d-setup";
constexpr const char* kApPsk = "esp-p4d-setup";
constexpr unsigned long kApAfterMs = 5UL * 60UL * 1000UL;  // so lange ohne Verbindung -> Access-Point dazu
constexpr unsigned long kRetryEveryMs = 60000;    // erneuter Verbindungsversuch bei aktivem AP
constexpr unsigned long kApOffAfterMs = 30000;    // so lange stabil verbunden -> Access-Point aus

// Wemos S2 Mini: bei voller Sendeleistung (19,5 dBm) war der Access-Point fuer
// andere Geraete unsichtbar (bekanntes Antennen-/Versorgungsproblem dieser
// Mini-Boards); mit 8,5 dBm sofort sichtbar (-50 dBm am PC daneben). Gilt fuer
// Access-Point und Client und muss nach jedem Moduswechsel neu gesetzt werden.
void applyTxPower() { WiFi.setTxPower(WIFI_POWER_8_5dBm); }
}  // namespace

const IPAddress NetworkManager::kApIp(192, 168, 4, 1);

String NetworkManager::sanitizeHostname(const String& name) {
  String out;
  for (size_t i = 0; i < name.length() && out.length() < 32; i++) {
    char c = tolower(name[i]);
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      out += c;
    } else if (out.length() && out[out.length() - 1] != '-') {
      out += '-';
    }
  }
  while (out.endsWith("-")) out.remove(out.length() - 1);
  return out.length() ? out : String("esp-p4d");
}

void NetworkManager::begin() {
  _hostname = sanitizeHostname(_config.getConfig().systemName);
  _apSsid = kApSsid;

  WiFi.persistent(false);  // Zugangsdaten liegen in config.json, nicht im WLAN-NVS
  WiFi.setHostname(_hostname.c_str());
  WiFi.setSleep(false);    // Erfahrung sensormeter-wlan: Modem-Sleep macht die Weboberflaeche traege/instabil
  WiFi.setAutoReconnect(true);

  if (_config.getConfig().wlanSsid.length()) {
    startStation();
  } else {
    Log::add("Kein WLAN eingerichtet - starte Einrichtungs-Access-Point");
    WiFi.mode(WIFI_AP);
    startAp();
  }
}

void NetworkManager::startStation() {
  const DeviceConfig& c = _config.getConfig();
  WiFi.mode(_apActive ? WIFI_AP_STA : WIFI_STA);
  applyTxPower();
  WiFi.begin(c.wlanSsid.c_str(), c.wlanPsk.c_str());
  _connectStartMs = _lastRetryMs = millis();
  Log::add("WLAN: verbinde mit \"%s\" ...", c.wlanSsid.c_str());
}

void NetworkManager::startAp() {
  if (_apActive) return;
  if (WiFi.getMode() == WIFI_STA) WiFi.mode(WIFI_AP_STA);
  bool ok = WiFi.softAP(_apSsid.c_str(), kApPsk);
  WiFi.softAPConfig(kApIp, kApIp, IPAddress(255, 255, 255, 0));
  applyTxPower();
  if (!ok) Log::add("Access-Point konnte nicht gestartet werden");
  _dns.setErrorReplyCode(DNSReplyCode::NoError);
  _dns.start(53, "*", kApIp);
  // Automatisches Wiederverbinden wechselt staendig den Kanal und wuerde
  // Geraete am Access-Point trennen - solange er laeuft, nur gezielt in loop().
  WiFi.setAutoReconnect(false);
  _apActive = true;
  Log::add("Access-Point \"%s\" aktiv - http://%s", _apSsid.c_str(), kApIp.toString().c_str());
}

void NetworkManager::stopAp() {
  if (!_apActive) return;
  _dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  applyTxPower();
  WiFi.setAutoReconnect(true);
  _apActive = false;
  Log::add("Access-Point aus (WLAN steht)");
}

void NetworkManager::requestConnect(const String& ssid, const String& psk) {
  _newSsid = ssid;
  _newPsk = psk;
  _connectRequested = true;
}

void NetworkManager::loop() {
  if (_apActive) _dns.processNextRequest();

  if (_connectRequested) {
    _connectRequested = false;
    DeviceConfig c = _config.getConfig();
    c.wlanSsid = _newSsid;
    c.wlanPsk = _newPsk;
    _config.setConfig(c);
    WiFi.disconnect();
    startStation();
  }

  bool connected = isConnected();
  unsigned long now = millis();

  if (connected && !_wasConnected) {
    _connectedSinceMs = now;
    Log::add("WLAN verbunden: %s, IP %s, Signal %d dBm", WiFi.SSID().c_str(),
             WiFi.localIP().toString().c_str(), WiFi.RSSI());
    if (!_mdnsStarted && MDNS.begin(_hostname.c_str())) {
      MDNS.addService("http", "tcp", 80);
      _mdnsStarted = true;
      Log::add("mDNS: http://%s.local", _hostname.c_str());
    }
  } else if (!connected && _wasConnected) {
    _connectStartMs = _lastRetryMs = now;
    Log::add("WLAN-Verbindung verloren");
  }
  _wasConnected = connected;

  if (connected) {
    if (_apActive && now - _connectedSinceMs > kApOffAfterMs) stopAp();
    return;
  }
  if (!_config.getConfig().wlanSsid.length()) return;  // nur Access-Point, nichts zu verbinden

  if (!_apActive && now - _connectStartMs > kApAfterMs) {
    Log::add("WLAN seit %lu s nicht erreichbar - Access-Point dazu", kApAfterMs / 1000);
    startAp();
  }
  if (_apActive && now - _lastRetryMs > kRetryEveryMs && WiFi.softAPgetStationNum() == 0) {
    _lastRetryMs = now;
    WiFi.begin(_config.getConfig().wlanSsid.c_str(), _config.getConfig().wlanPsk.c_str());
  }
}
