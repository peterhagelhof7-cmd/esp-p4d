#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>

#include "ConfigManager.h"

// WLAN-only (der S2 hat kein LAN). Ablauf:
//  - kein WLAN gespeichert -> sofort Einrichtungs-Access-Point "esp-p4d-setup"
//    (WPA2, Passwort "esp-p4d-setup", http://192.168.4.1; Captive-Portal: jede
//    Adresse fuehrt auf die Weboberflaeche)
//  - WLAN gespeichert -> verbinden; klappt das 5 min lang nicht, kommt der
//    Access-Point zusaetzlich dazu. Der Client versucht es alle 60 s erneut,
//    aber nur solange niemand am Access-Point haengt (ein Verbindungsversuch
//    wechselt den Kanal und wuerde ihn trennen).
//  - steht die Verbindung 30 s stabil, geht der Access-Point wieder aus.
// Hostname/mDNS aus dem Systemnamen: http://esp-p4d.local
class NetworkManager {
 public:
  explicit NetworkManager(ConfigManager& config) : _config(config) {}

  void begin();
  void loop();

  // Neues WLAN speichern und verbinden (aus loop() heraus, nicht im HTTP-Task).
  void requestConnect(const String& ssid, const String& psk);

  bool isConnected() const { return WiFi.status() == WL_CONNECTED; }
  bool isApActive() const { return _apActive; }
  String apSsid() const { return _apSsid; }
  String hostname() const { return _hostname; }

  // Leitet aus dem Systemnamen einen DNS-/mDNS-tauglichen Hostnamen ab (a-z/0-9/-).
  static String sanitizeHostname(const String& name);

  static const IPAddress kApIp;

 private:
  ConfigManager& _config;
  DNSServer _dns;
  String _hostname;
  String _apSsid;
  bool _apActive = false;
  bool _mdnsStarted = false;
  bool _wasConnected = false;
  unsigned long _connectStartMs = 0;
  unsigned long _connectedSinceMs = 0;
  unsigned long _lastRetryMs = 0;

  bool _connectRequested = false;
  String _newSsid;
  String _newPsk;

  void startAp();
  void stopAp();
  void startStation();
};
