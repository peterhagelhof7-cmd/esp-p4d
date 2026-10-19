#include "WebServerManager.h"

#include <ArduinoJson.h>
#include <WiFi.h>

#include "ComMonitor.h"
#include "Log.h"
#include "NetCheck.h"
#include "WebPages.h"
#include "version.h"

namespace {
time_t validTime() {
  time_t now = time(nullptr);
  return now > 1700000000 ? now : 0;  // vor NTP-Sync: 1970
}
}  // namespace

bool WebServerManager::checkAuth(AsyncWebServerRequest* request) {
  if (!request->authenticate("admin", _config.getConfig().settingsPassword.c_str())) {
    request->requestAuthentication("ESP-P4d (Benutzername: admin)");
    return false;
  }
  return true;
}

// Maschinen-API: Header X-API-Key (wie beim MeshCore-Gateway fuer home-central).
bool WebServerManager::checkApiKey(AsyncWebServerRequest* request) {
  const String& key = _config.getConfig().apiKey;
  if (!request->hasHeader("X-API-Key") || key.isEmpty() || request->header("X-API-Key") != key) {
    request->send(401, "application/json", "{\"error\":\"X-API-Key fehlt oder falsch\"}");
    return false;
  }
  return true;
}

String WebServerManager::deviceJson() {
  const DeviceConfig& c = _config.getConfig();
  JsonDocument doc;
  doc["systemName"] = c.systemName;
  doc["firmware"] = DEVICE_FIRMWARE_VERSION;
  doc["uptime"] = Log::uptimeMs() / 1000;
  doc["heap"] = ESP.getFreeHeap();
  doc["time"] = (unsigned long)validTime();
  doc["defaultPassword"] = c.settingsPassword == "admin";
  JsonObject w = doc["wlan"].to<JsonObject>();
  w["connected"] = _net.isConnected();
  w["configuredSsid"] = c.wlanSsid;
  w["ssid"] = WiFi.SSID();
  w["ip"] = WiFi.localIP().toString();
  w["rssi"] = WiFi.RSSI();
  w["hostname"] = _net.hostname();
  w["static"] = _net.staticActive();
  w["ipNote"] = _net.ipNote();
  JsonObject ap = doc["ap"].to<JsonObject>();
  ap["active"] = _net.isApActive();
  ap["ssid"] = _net.apSsid();
  String out;
  serializeJson(doc, out);
  return out;
}

// {"device":{...},"kessel":{...}}
void WebServerManager::handleStatus(AsyncWebServerRequest* request) {
  String out = "{\"device\":";
  out += deviceJson();
  out += ",\"kessel\":";
  out += _kessel.statusJson();
  out += "}";
  request->send(200, "application/json", out);
}

// Asynchrone Suche: erster Aufruf startet sie, die Seite fragt nach, bis sie fertig ist.
void WebServerManager::handleWifiScan(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true);
    n = WIFI_SCAN_RUNNING;
  }
  JsonDocument doc;
  doc["running"] = n == WIFI_SCAN_RUNNING;
  JsonArray arr = doc["networks"].to<JsonArray>();
  if (n >= 0) {
    for (int i = 0; i < n; i++) {
      if (WiFi.SSID(i).isEmpty()) continue;
      JsonObject o = arr.add<JsonObject>();
      o["ssid"] = WiFi.SSID(i);
      o["rssi"] = WiFi.RSSI(i);
      o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
  }
  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

void WebServerManager::handleWifiConnect(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;
  String ssid = request->hasParam("ssid", true) ? request->getParam("ssid", true)->value() : "";
  String psk = request->hasParam("psk", true) ? request->getParam("psk", true)->value() : "";
  ssid.trim();
  if (ssid.isEmpty() || ssid.length() > 32 || psk.length() > 63 || (psk.length() > 0 && psk.length() < 8)) {
    request->send(400, "text/plain", "Ungueltig: SSID 1-32 Zeichen, Passwort leer (offenes WLAN) oder 8-63 Zeichen.");
    return;
  }
  Log::add("WLAN-Einstellung geaendert auf \"%s\"", ssid.c_str());
  _net.requestConnect(ssid, psk);
  request->send(200, "text/plain",
                "Gespeichert. Das Geraet verbindet sich jetzt mit \"" + ssid + "\" und ist danach unter http://" +
                    _net.hostname() + ".local erreichbar (oder die Adresse im Router nachsehen). " +
                    "Das Einrichtungs-WLAN geht 30 s nach erfolgreicher Verbindung aus.");
}

void WebServerManager::handleConfig(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;
  DeviceConfig c = _config.getConfig();
  if (request->hasParam("systemName", true)) {
    String n = request->getParam("systemName", true)->value();
    n.trim();
    if (n.length() && n.length() <= 32) c.systemName = n;
  }
  if (request->hasParam("ntpServer", true)) {
    String n = request->getParam("ntpServer", true)->value();
    n.trim();
    if (n.length() <= 64) c.ntpServer = n;
  }
  if (request->hasParam("newPassword", true)) {
    String pw = request->getParam("newPassword", true)->value();
    if (pw.length() && pw.length() < 6) {
      request->send(400, "text/plain", "Passwort zu kurz (mindestens 6 Zeichen) - nichts gespeichert.");
      return;
    }
    if (pw.length()) c.settingsPassword = pw;
  }
  _config.setConfig(c);
  Log::add("Einstellungen gespeichert (Name \"%s\")", c.systemName.c_str());
  request->send(200, "text/plain", "Gespeichert. Ein neuer Geraetename und NTP-Server gelten nach einem Neustart.");
}

// sel = "0x0000,0x0012,..." (leer = alle Werte in der Zusammenfassung)
void WebServerManager::handleSelection(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;
  String sel = request->hasParam("sel", true) ? request->getParam("sel", true)->value() : "";
  DeviceConfig c = _config.getConfig();
  c.summary.clear();
  int start = 0;
  while (start < (int)sel.length() && c.summary.size() < 100) {
    int comma = sel.indexOf(',', start);
    if (comma < 0) comma = sel.length();
    String part = sel.substring(start, comma);
    part.trim();
    if (part.length()) c.summary.push_back((uint16_t)strtoul(part.c_str(), nullptr, 16));
    start = comma + 1;
  }
  _config.setConfig(c);
  Log::add("Werte-Auswahl fuer home-central: %u Werte", (unsigned)c.summary.size());
  request->send(200, "text/plain",
                c.summary.empty() ? String("Gespeichert: keine Auswahl - home-central bekommt alle Werte.")
                                  : "Gespeichert: " + String(c.summary.size()) + " Werte fuer home-central.");
}

void WebServerManager::handleRestore(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;
  String error;
  if (_restoreBody.isEmpty()) {
    request->send(400, "text/plain", "Keine Datei empfangen.");
    return;
  }
  bool ok = _config.restoreJson(_restoreBody, error);
  _restoreBody = String();
  if (!ok) {
    Log::add("Backup abgelehnt: %s", error.c_str());
    request->send(400, "text/plain", "Backup abgelehnt: " + error + " - nichts geaendert.");
    return;
  }
  Log::add("Einstellungs-Backup eingespielt, Neustart");
  request->send(200, "text/plain", "Backup eingespielt, Geraet startet neu ...");
  scheduleRestart();
}

// OTA-Upload - Muster aus ESP-Anwesenheit/sensormeter: _otaInProgress/_otaSuccess
// werden im Upload-Callback gesetzt und im danach aufgerufenen Request-Callback ausgewertet.
void WebServerManager::handleOtaRequest(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;
  if (_otaSuccess) {
    Log::add("OTA erfolgreich, Neustart");
    request->send(200, "text/plain", "Update erfolgreich, Geraet startet neu ...");
    scheduleRestart();
  } else if (!_otaInProgress) {
    Log::add("OTA fehlgeschlagen (Schreibfehler)");
    request->send(500, "text/plain", "Update fehlgeschlagen (Schreibfehler)");
  } else if (!_ota.markerFound()) {
    Log::add("OTA abgelehnt: kein Firmware-Erkennungsmerkmal");
    request->send(400, "text/plain", "Update abgelehnt: kein gueltiges Firmware-Erkennungsmerkmal gefunden.");
  } else if (!_ota.identityMatches()) {
    Log::add("OTA abgelehnt: .bin eines anderen Projekts");
    request->send(400, "text/plain", "Update abgelehnt: die Datei stammt von einem anderen Projekt.");
  } else if (!_ota.versionAllowed()) {
    Log::add("OTA abgelehnt: aeltere Version");
    request->send(400, "text/plain", "Update abgelehnt: aeltere Version (Downgrade nicht erlaubt).");
  } else {
    Log::add("OTA fehlgeschlagen");
    request->send(500, "text/plain", "Update fehlgeschlagen");
  }
}

void WebServerManager::handleOtaUpload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data,
                                       size_t len, bool final) {
  if (!checkAuth(request)) return;
  if (index == 0) {
    _ota.setAllowDowngrade(request->hasParam("otaForceDowngrade", true));
    // contentLength() statt UPDATE_SIZE_UNKNOWN: sonst loescht Update.begin() die
    // ganze OTA-Partition vorab (lange Blockade, bei sensormeter-wlan WLAN-Abbruch).
    _otaInProgress = _ota.beginLocalUpdate(request->contentLength());
    _otaSuccess = false;
  }
  if (_otaInProgress) _otaInProgress = _ota.writeLocalUpdateChunk(data, len);
  if (final && _otaInProgress) _otaSuccess = _ota.endLocalUpdate();
}

// Captive-Portal: im Einrichtungs-WLAN jede fremde Adresse auf die Startseite.
void WebServerManager::handleNotFound(AsyncWebServerRequest* request) {
  if (_net.isApActive() && request->host() != NetworkManager::kApIp.toString()) {
    request->redirect("http://" + NetworkManager::kApIp.toString() + "/");
    return;
  }
  request->send(404, "text/plain", "Nicht gefunden");
}

void WebServerManager::begin() {
  // Alle Routen exakt: ein String-URI matcht sonst auch Unterpfade (siehe
  // Notiz zu ESPAsyncWebServer BackwardCompatible-Match).
  auto page = [this](const char* uri, const char* html, bool auth) {
    _server.on(AsyncURIMatcher::exact(uri), HTTP_GET, [this, html, auth](AsyncWebServerRequest* r) {
      if (!auth || checkAuth(r)) r->send(200, "text/html", html);
    });
  };
  page("/", kStatusPage, false);
  page("/values", kValuesPage, false);
  page("/console", kConsolePage, true);
  page("/settings", kSettingsPage, true);
  _server.on(AsyncURIMatcher::exact("/style.css"), HTTP_GET,
             [](AsyncWebServerRequest* r) { r->send(200, "text/css", kPageStyle); });

  _server.on(AsyncURIMatcher::exact("/api/status"), HTTP_GET, [this](AsyncWebServerRequest* r) { handleStatus(r); });
  _server.on(AsyncURIMatcher::exact("/api/values"), HTTP_GET, [this](AsyncWebServerRequest* r) {
    r->send(200, "application/json", _kessel.valuesJson(_config.getConfig().summary));
  });
  _server.on(AsyncURIMatcher::exact("/api/console"), HTTP_GET, [this](AsyncWebServerRequest* r) {
    if (!checkAuth(r)) return;
    uint32_t since = r->hasParam("since") ? strtoul(r->getParam("since")->value().c_str(), nullptr, 10) : 0;
    r->send(200, "application/json", ComMonitor::json(since));
  });
  _server.on(AsyncURIMatcher::exact("/api/settings"), HTTP_GET, [this](AsyncWebServerRequest* r) {
    if (!checkAuth(r)) return;
    JsonDocument doc;
    doc["systemName"] = _config.getConfig().systemName;
    doc["ntpServer"] = _config.getConfig().ntpServer;
    doc["apiKey"] = _config.getConfig().apiKey;
    doc["wlanSsid"] = _config.getConfig().wlanSsid;
    // Feste IP: Einstellung + aktuelle Werte (zum Vorbelegen der Felder)
    const DeviceConfig& c = _config.getConfig();
    JsonObject ip = doc["ip"].to<JsonObject>();
    ip["addr"] = c.ipAddr;
    ip["mask"] = c.ipMask;
    ip["gw"] = c.ipGw;
    ip["dns1"] = c.ipDns1;
    ip["dns2"] = c.ipDns2;
    ip["pending"] = c.ipPendingCheck;
    ip["static"] = _net.staticActive();
    ip["note"] = _net.ipNote();
    bool sta = _net.isConnected();
    ip["curAddr"] = sta ? WiFi.localIP().toString() : String();
    ip["curMask"] = sta ? WiFi.subnetMask().toString() : String();
    ip["curGw"] = sta ? WiFi.gatewayIP().toString() : String();
    ip["curDns"] = sta ? WiFi.dnsIP(0).toString() : String();
    String out;
    serializeJson(doc, out);
    r->send(200, "application/json", out);
  });
  _server.on(AsyncURIMatcher::exact("/api/backup"), HTTP_GET, [this](AsyncWebServerRequest* r) {
    if (!checkAuth(r)) return;
    AsyncWebServerResponse* res = r->beginResponse(200, "application/json", _config.toJson());
    res->addHeader("Content-Disposition",
                   "attachment; filename=\"" + _net.hostname() + "-einstellungen.json\"");
    r->send(res);
  });
  _server.on(AsyncURIMatcher::exact("/log.txt"), HTTP_GET, [this](AsyncWebServerRequest* r) {
    if (checkAuth(r)) r->send(200, "text/plain; charset=utf-8", Log::text());
  });
  _server.on(AsyncURIMatcher::exact("/api/wifi/scan"), HTTP_GET,
             [this](AsyncWebServerRequest* r) { handleWifiScan(r); });

  _server.on(AsyncURIMatcher::exact("/api/wifi/connect"), HTTP_POST,
             [this](AsyncWebServerRequest* r) { handleWifiConnect(r); });
  _server.on(AsyncURIMatcher::exact("/api/config"), HTTP_POST, [this](AsyncWebServerRequest* r) { handleConfig(r); });
  _server.on(AsyncURIMatcher::exact("/api/selection"), HTTP_POST,
             [this](AsyncWebServerRequest* r) { handleSelection(r); });
  _server.on(AsyncURIMatcher::exact("/api/apikey"), HTTP_POST, [this](AsyncWebServerRequest* r) {
    if (!checkAuth(r)) return;
    DeviceConfig c = _config.getConfig();
    c.apiKey = ConfigManager::randomKey();
    _config.setConfig(c);
    Log::add("Neuer API-Schluessel erzeugt");
    r->send(200, "text/plain", c.apiKey);
  });
  // Feste IP (Mechanik wie esp-meshcore-gateway E40): ip leer = DHCP. Gilt nach
  // dem Neustart; eine neue Adresse wird dann erst geprueft (NetworkManager).
  _server.on(AsyncURIMatcher::exact("/api/network"), HTTP_POST, [this](AsyncWebServerRequest* r) {
    if (!checkAuth(r)) return;
    auto p = [r](const char* n) { return r->hasParam(n, true) ? r->getParam(n, true)->value() : String(); };
    DeviceConfig c = _config.getConfig();
    String err;
    if (!ConfigManager::applyIp(c, p("ip"), p("mask"), p("gw"), p("dns1"), p("dns2"), err)) {
      r->send(400, "text/plain", err + " - nichts gespeichert.");
      return;
    }
    if (c.staticIp()) c.ipPendingCheck = true;  // auch gleiche Adresse: beim Start neu pruefen
    _config.setConfig(c);
    Log::add("Adressvergabe gespeichert: %s, Neustart", c.staticIp() ? ("feste IP " + c.ipAddr).c_str() : "DHCP");
    r->send(200, "text/plain",
            c.staticIp() ? "Gespeichert, Geraet startet neu. Es meldet sich erst per DHCP an, prueft die Adresse und "
                           "schaltet dann auf " + c.ipAddr + " um (Ergebnis im Ereignis-Log)."
                         : String("Gespeichert, Geraet startet neu und holt sich die Adresse per DHCP."));
    scheduleRestart();
  });
  // Vorab-Pruefung, ob eine Adresse belegt ist: POST startet, GET liefert den Stand.
  _server.on(AsyncURIMatcher::exact("/api/checkip"), HTTP_POST, [this](AsyncWebServerRequest* r) {
    if (!checkAuth(r)) return;
    IPAddress ip;
    String s = r->hasParam("ip", true) ? r->getParam("ip", true)->value() : String();
    s.trim();
    if (!ip.fromString(s)) {
      r->send(400, "text/plain", "IP-Adresse ungueltig");
      return;
    }
    if (!net_check::probe(ip)) {
      r->send(409, "text/plain", "Es laeuft schon eine Pruefung, gleich noch einmal");
      return;
    }
    r->send(200, "text/plain", "ok");
  });
  _server.on(AsyncURIMatcher::exact("/api/checkip"), HTTP_GET, [this](AsyncWebServerRequest* r) {
    if (!checkAuth(r)) return;
    char detail[48];
    net_check::State st = net_check::result(detail, sizeof(detail));
    JsonDocument doc;
    doc["state"] = net_check::stateName(st);
    doc["detail"] = detail;
    String out;
    serializeJson(doc, out);
    r->send(200, "application/json", out);
  });
  _server.on(AsyncURIMatcher::exact("/api/rescan"), HTTP_POST, [this](AsyncWebServerRequest* r) {
    if (!checkAuth(r)) return;
    _kessel.requestRescan();
    r->send(200, "text/plain", "Werte-Katalog wird neu gelesen.");
  });
  _server.on(AsyncURIMatcher::exact("/api/reboot"), HTTP_POST, [this](AsyncWebServerRequest* r) {
    if (!checkAuth(r)) return;
    Log::add("Neustart ueber die Weboberflaeche");
    r->send(200, "text/plain", "Geraet startet neu ...");
    scheduleRestart();
  });
  // Roher JSON-Body: 5. Argument = onBody (4. = onUpload bleibt leer).
  _server.on(
      AsyncURIMatcher::exact("/api/restore"), HTTP_POST, [this](AsyncWebServerRequest* r) { handleRestore(r); },
      nullptr,
      [this](AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t index, size_t total) {
        if (index == 0) _restoreBody = String();
        if (total > 8192) return;  // ein Einstellungs-Backup ist klein
        _restoreBody.concat((const char*)data, len);
      });
  // Multipart-Upload: 4. Argument = onUpload (nicht onBody).
  _server.on(
      AsyncURIMatcher::exact("/update"), HTTP_POST, [this](AsyncWebServerRequest* r) { handleOtaRequest(r); },
      [this](AsyncWebServerRequest* r, String filename, size_t index, uint8_t* data, size_t len, bool final) {
        handleOtaUpload(r, filename, index, data, len, final);
      });

  // Maschinen-API fuer home-central
  _server.on(AsyncURIMatcher::exact("/api/v1/summary"), HTTP_GET, [this](AsyncWebServerRequest* r) {
    if (!checkApiKey(r)) return;
    r->send(200, "application/json",
            _kessel.summaryJson(_config.getConfig().summary, _config.getConfig().systemName, validTime()));
  });
  _server.on(AsyncURIMatcher::exact("/api/v1/values"), HTTP_GET, [this](AsyncWebServerRequest* r) {
    if (!checkApiKey(r)) return;
    r->send(200, "application/json", _kessel.valuesJson(_config.getConfig().summary));
  });
  _server.on(AsyncURIMatcher::exact("/api/v1/status"), HTTP_GET, [this](AsyncWebServerRequest* r) {
    if (!checkApiKey(r)) return;
    handleStatus(r);
  });

  _server.onNotFound([this](AsyncWebServerRequest* r) { handleNotFound(r); });
  _server.begin();
}

void WebServerManager::loop() {
  if (_restartAtMs && (long)(millis() - _restartAtMs) >= 0) {
    delay(100);
    ESP.restart();
  }
}
