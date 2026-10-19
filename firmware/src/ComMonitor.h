#pragma once

#include <Arduino.h>

// Mitschnitt der COM1-Kommunikation fuer die Webkonsole: Ringpuffer der
// letzten Rahmen (Richtung, Kommando, Rohbytes als Hex, OK/Fehler), jeder mit
// fortlaufender Nummer, damit die Seite nur Neues nachladen kann.
namespace ComMonitor {
// Passt zur Signatur von P4Com::TrafficHook.
void record(bool tx, uint8_t command, const uint8_t* raw, int len, bool ok);
// JSON {"next":<seq>,"lines":[{"s":seq,"t":ms,"d":"tx|rx","c":"0x30","n":"GetValue","h":"02 FD ...","ok":true}]}
// mit allen Eintraegen ab Nummer since.
String json(uint32_t since);
}  // namespace ComMonitor
