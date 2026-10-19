#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "ConfigManager.h"
#include "KesselManager.h"
#include "NetworkManager.h"
#include "OtaManager.h"

// Weboberflaeche (Benutzer admin fuer alles, was aendert oder Geheimes zeigt):
//   GET  /                  Status: Kessel, ausgewaehlte Werte, WLAN, Geraet
//   GET  /values            alle Werte des Kessels, Auswahl fuer home-central
//   GET  /console           COM1-Mitschnitt live (admin)
//   GET  /settings          WLAN, Name, Passwort, NTP, API-Schluessel, Backup,
//                           Firmware-Update, Neustart (admin)
//   GET  /api/status, /api/values                 fuer die Seiten
//   GET  /api/console?since=N, /api/settings, /api/backup, /log.txt   (admin)
//   POST /api/wifi/connect, /api/network (feste IP), /api/checkip, /api/config,
//        /api/selection, /api/apikey,
//        /api/rescan, /api/restore, /api/reboot, /update               (admin)
// Maschinen-API fuer home-central (Header X-API-Key):
//   GET  /api/v1/summary    Zustand + ausgewaehlte Werte (leer = alle)
//   GET  /api/v1/values     alle Werte mit Faktor/Rohwert/Alter
//   GET  /api/v1/status     Kessel-/Geraetestatus inkl. Fehlerpuffer
// Im Einrichtungs-Access-Point leitet jede fremde Adresse auf die Startseite
// um (Captive-Portal), damit Handys die Seite von selbst oeffnen.
class WebServerManager {
 public:
  WebServerManager(ConfigManager& config, NetworkManager& net, OtaManager& ota, KesselManager& kessel)
      : _server(80), _config(config), _net(net), _ota(ota), _kessel(kessel) {}

  void begin();
  // Neustart nach OTA/Restore/auf Anforderung - aus loop() heraus ausgefuehrt.
  void loop();

 private:
  AsyncWebServer _server;
  ConfigManager& _config;
  NetworkManager& _net;
  OtaManager& _ota;
  KesselManager& _kessel;

  bool _otaInProgress = false;
  bool _otaSuccess = false;
  unsigned long _restartAtMs = 0;
  String _restoreBody;

  bool checkAuth(AsyncWebServerRequest* request);
  bool checkApiKey(AsyncWebServerRequest* request);
  void scheduleRestart() { _restartAtMs = millis() + 800; }
  String deviceJson();

  void handleStatus(AsyncWebServerRequest* request);
  void handleWifiScan(AsyncWebServerRequest* request);
  void handleWifiConnect(AsyncWebServerRequest* request);
  void handleConfig(AsyncWebServerRequest* request);
  void handleSelection(AsyncWebServerRequest* request);
  void handleRestore(AsyncWebServerRequest* request);
  void handleOtaRequest(AsyncWebServerRequest* request);
  void handleOtaUpload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len,
                       bool final);
  void handleNotFound(AsyncWebServerRequest* request);
};
