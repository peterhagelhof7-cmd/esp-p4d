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
//   "wlan": {"ssid": "", "psk": "",
//            "ip": "", "mask": "", "gw": "", "dns1": "", "dns2": "", "ipCheck": false}
// }
// Feste IP wie beim esp-meshcore-gateway (E40): ip leer = DHCP; ipCheck = neue
// Adresse beim naechsten Start erst pruefen (siehe NetworkManager).

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
  // Feste IP: ipAddr leer = DHCP, dns1 leer = Router als DNS.
  String ipAddr, ipMask, ipGw, ipDns1, ipDns2;
  // Einmal-Flag: neue feste IP beim naechsten Verbinden erst per DHCP ins Netz,
  // Adresse auf Belegung pruefen, umschalten, Router pruefen; bei Fehler DHCP.
  bool ipPendingCheck = false;
  bool staticIp() const { return ipAddr.length() > 0; }
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

  // Feste IP pruefen und normiert in cfg uebernehmen (ip leer = DHCP). false +
  // Grund bei ungueltigen Werten. Neue Adresse setzt ipPendingCheck.
  static bool applyIp(DeviceConfig& cfg, const String& ip, const String& mask, const String& gw,
                      const String& dns1, const String& dns2, String& error);

 private:
  DeviceConfig _config;
  bool load();
  static bool parse(const String& json, DeviceConfig& cfg, String& error);
};
