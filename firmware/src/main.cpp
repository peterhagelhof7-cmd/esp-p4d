// ***************************************************************************
// ESP-P4d - Froeling Lambdatronic S3200 ueber COM1 auslesen (Wemos S2 Mini)
//
// Stand 0.2.0: COM1 (Zustand, Werte-Katalog, Werte, Fehlerpuffer), WLAN mit
// Einrichtungs-Access-Point, Weboberflaeche mit COM1-Konsole, Einstellungs-
// Backup, OTA-Update und API fuer home-central. Historie/CSV und MQTT folgen.
// ***************************************************************************
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>

#include "ConfigManager.h"
#include "ComMonitor.h"
#include "KesselManager.h"
#include "Log.h"
#include "NetworkManager.h"
#include "OtaManager.h"
#include "P4Com.h"
#include "WebServerManager.h"
#include "version.h"

// UART1 (Serial1) zum MAX3232 an COM1 des Kessels.
#ifndef P4_RX_PIN
#define P4_RX_PIN 18   // <- MAX3232 R1OUT  (Kessel-TX)
#endif
#ifndef P4_TX_PIN
#define P4_TX_PIN 17   // -> MAX3232 T1IN   (Kessel-RX)
#endif

// Eingebetteter Marker fuer die OTA-Herkunfts-/Versionspruefung (siehe
// OtaManager) - in setup() ausgegeben, damit der Linker ihn nicht wegoptimiert.
const char kFirmwareIdentityMarker[] = "SM-FW-ID:" FIRMWARE_PROJECT_ID ":" DEVICE_FIRMWARE_VERSION ":SM-FW-END";

P4Com p4(Serial1, P4_RX_PIN, P4_TX_PIN);
ConfigManager configManager;
NetworkManager networkManager(configManager);
OtaManager otaManager;
KesselManager kessel(p4);
WebServerManager webServer(configManager, networkManager, otaManager, kessel);

void setup() {
  Log::markBoot();
  Serial.begin(115200);   // USB-CDC (Debug-Ausgabe)
  delay(1500);            // dem USB-CDC Zeit geben, sich zu verbinden
  Serial.printf("\n=== ESP-P4d %s ===\n", DEVICE_FIRMWARE_VERSION);
  Serial.println(kFirmwareIdentityMarker);

  if (!LittleFS.begin(true)) Serial.println("[FS] LittleFS konnte nicht eingebunden werden");
  configManager.begin();

  Log::add("Start, Firmware %s, COM1: RX=GPIO%d TX=GPIO%d", DEVICE_FIRMWARE_VERSION, P4_RX_PIN, P4_TX_PIN);
  networkManager.begin();
  webServer.begin();
  p4.setTrafficHook(ComMonitor::record);
  p4.begin();
  kessel.begin();

  // Task-Watchdog (10 s, Panic -> Neustart) fuer den Loop-Task, wie in der
  // sensormeter-Familie. Der OtaManager traegt ihn waehrend eines Uploads aus.
  esp_task_wdt_init(10, true);
  esp_task_wdt_add(NULL);
  otaManager.setMainLoopTaskHandle(xTaskGetCurrentTaskHandle());
}

// USB-Konsole: "l" = Ereignis-Log, "s" = Kurzstatus, "w" = WLAN-Suche,
// "j" = API-JSON, "c" = COM1-Mitschnitt (die
// Startmeldungen sind meist schon durch, bevor ein Terminal den Port oeffnet).
static bool consoleScan = false;

static void handleConsole() {
  if (consoleScan && WiFi.scanComplete() >= 0) {
    int n = WiFi.scanComplete();
    Serial.printf("WLAN-Suche: %d Netze | AP \"%s\" Kanal %d | Sendeleistung %d (0,25 dBm)\n", n,
                  WiFi.softAPSSID().c_str(), WiFi.channel(), (int)WiFi.getTxPower());
    for (int i = 0; i < n && i < 8; i++) Serial.printf("  %s %d dBm\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    WiFi.scanDelete();
    consoleScan = false;
  }
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'l') {
      Serial.print(Log::text());
    } else if (c == 'w') {
      // asynchron - eine blockierende Suche kann den 10-s-Watchdog ausloesen
      WiFi.scanNetworks(true);
      consoleScan = true;
      Serial.println("WLAN-Suche laeuft ...");
    } else if (c == 'j') {
      // dieselben JSON-Antworten wie /api/v1/status, /api/v1/summary (ohne WLAN pruefbar)
      Serial.println(kessel.statusJson());
      Serial.println(kessel.summaryJson(configManager.getConfig().summary, configManager.getConfig().systemName,
                                        time(nullptr) > 1700000000 ? time(nullptr) : 0));
    } else if (c == 'c') {
      Serial.println(ComMonitor::json(0));
    } else if (c == 's') {
      Serial.printf("Firmware %s | WLAN %s (%s, %s) | AP %s (%s) | Heizung %s | Heap %u\n", DEVICE_FIRMWARE_VERSION,
                    networkManager.isConnected() ? "verbunden" : "getrennt",
                    configManager.getConfig().wlanSsid.c_str(), WiFi.localIP().toString().c_str(),
                    networkManager.isApActive() ? "an" : "aus", WiFi.softAPIP().toString().c_str(),
                    kessel.connected() ? "antwortet" : "keine Antwort", (unsigned)ESP.getFreeHeap());
    }
  }
}

// Zeit per NTP, sobald das WLAN steht (fuer Zeitstempel in der API). Zeitzone
// Mitteleuropa mit Sommerzeit.
static void startNtpOnce() {
  static bool started = false;
  if (started || !networkManager.isConnected()) return;
  started = true;
  const String& server = configManager.getConfig().ntpServer;
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", server.length() ? server.c_str() : "pool.ntp.org");
  Log::add("NTP: %s", server.length() ? server.c_str() : "pool.ntp.org");
}

void loop() {
  esp_task_wdt_reset();
  handleConsole();
  networkManager.loop();
  webServer.loop();
  otaManager.checkStalled();
  startNtpOnce();
  kessel.loop();
  delay(2);
}
