#pragma once

#include <Arduino.h>

#include <vector>

// Laufzeitkonfiguration, persistiert als config.json auf LittleFS (Muster aus
// ESP-Anwesenheit, auf das reduziert, was der WLAN-only-S2 braucht).
//
// Schema (config.json, zugleich Format des Einstellungs-Backups):
// {
//   "project": "ESP-P4D",
//   "systemName": "ESP-P4d",
//   "settingsPassword": "admin",
//   "apiKey": "<32 Hex-Zeichen>",
//   "ntpServer": "pool.ntp.org",
//   "summary": ["0x0000", ...],          // Werte fuer /api/v1/summary (leer = alle)
//   "wlan": {"ssid": "", "psk": ""}
// }

struct DeviceConfig {
  String systemName = "ESP-P4d";
  // Benutzername fest "admin", Default-Passwort "admin" (Weboberflaeche weist
  // darauf hin, bis es geaendert ist).
  String settingsPassword = "admin";
  // Schluessel fuer die Maschinen-API (/api/v1/*, Header X-API-Key) - wird beim
  // ersten Start zufaellig erzeugt.
  String apiKey;
  String ntpServer = "pool.ntp.org";
  std::vector<uint16_t> summary;
  String wlanSsid;
  String wlanPsk;
};

class ConfigManager {
 public:
  // Laedt config.json von LittleFS. Fehlt die Datei oder ist sie ungueltig,
  // werden Defaults verwendet und sofort gespeichert.
  void begin();

  const DeviceConfig& getConfig() const { return _config; }

  // Uebernimmt eine neue Konfiguration und speichert sie sofort.
  void setConfig(const DeviceConfig& config);

  bool save();

  // Einstellungs-Backup: komplette Konfiguration als JSON (mit Passwoertern).
  String toJson() const;
  // Backup einspielen: prueft und uebernimmt; false + Grund bei Fehler.
  bool restoreJson(const String& json, String& error);

  static String randomKey();

 private:
  DeviceConfig _config;
  bool load();
  static bool parse(const String& json, DeviceConfig& cfg, String& error);
};
