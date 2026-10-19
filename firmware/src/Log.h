#pragma once

#include <Arduino.h>

// Kleiner Ringpuffer fuer Ereignisse (WLAN, OTA, Kessel-Verbindung). Liegt nur
// im RAM - kein Flash-Verschleiss; nach einem Neustart beginnt er leer. Jede
// Zeile geht zusaetzlich auf die USB-Konsole. Download: GET /log.txt.
namespace Log {
void add(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
// Alle Zeilen, aelteste zuerst, je mit Laufzeit-Stempel.
String text();
}  // namespace Log
