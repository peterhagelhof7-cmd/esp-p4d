#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Vitalwerte des Boards fuer /api/status und /api/v1/status ("vital"):
// Chip-Temperatur, Neustart-Grund und -Zaehler, Speicher (aktuell, kleinster seit
// Start, groesster Block, PSRAM), Hauptschleife (laengster Durchlauf, Stack-Reserve),
// Dateisystem und letzte NTP-Synchronisierung. WLAN-Details liefert der
// NetworkManager, COM1-Statistik der KesselManager.
namespace Vitals {

void begin();  // in setup(): Neustart-Zaehler (NVS) hochzaehlen, Grund merken, NTP-Callback
void loop();   // in loop(): Laufzeit messen, Temperatur/Stack zyklisch lesen

const char* resetReason();  // "poweron", "panic", "task_wdt", "brownout", ... (stabile Kennung)
bool abnormalReset();       // Absturz, Watchdog oder Spannungseinbruch
uint32_t bootCount();

void toJson(JsonObject o);

}  // namespace Vitals
