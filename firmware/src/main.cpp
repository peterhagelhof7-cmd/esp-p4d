// ***************************************************************************
// ESP-P4d - Milestone 1: COM1-Proof-of-Life
//
// Fragt den Fröling-Kessel (Lambdatronic S3200) zyklisch per cmdCheck (0x22)
// und cmdGetVersion (0x41) ab und gibt das Ergebnis ueber den nativen USB
// (USB-CDC = "Serial") aus. Damit sieht man an der Anlage, ob die Verbindung
// ueber COM1 steht - und ob TX/RX am MAX3232 richtig herum sind.
//
// Netz/Web/OTA/Historie/CSV/MQTT folgen in spaeteren Milestones.
// ***************************************************************************
#include <Arduino.h>

#include "P4Com.h"

// UART1 (Serial1) zum MAX3232 an COM1 des Kessels. Pins am Wemos S2 Mini frei
// waehlbar; hier bewusst als #define, damit sie leicht anzupassen sind.
#ifndef P4_RX_PIN
#define P4_RX_PIN 18   // <- MAX3232 R1OUT  (Kessel-TX)
#endif
#ifndef P4_TX_PIN
#define P4_TX_PIN 17   // -> MAX3232 T1IN   (Kessel-RX)
#endif

P4Com p4(Serial1, P4_RX_PIN, P4_TX_PIN);

void setup() {
  Serial.begin(115200);   // USB-CDC (Debug-Ausgabe)
  delay(2000);            // dem USB-CDC Zeit geben, sich zu verbinden
  Serial.println();
  Serial.println("=== ESP-P4d - Proof-of-Life (Fröling COM1, 57600 8N1) ===");
  Serial.printf("UART1: RX=GPIO%d (Kessel-TX), TX=GPIO%d (Kessel-RX)\n", P4_RX_PIN, P4_TX_PIN);
  Serial.println("Sende zyklisch cmdCheck (0x22) + cmdGetVersion (0x41) ...");
  Serial.println("Kommt keine Antwort: TX/RX am MAX3232 tauschen, GND pruefen,");
  Serial.println("und sicherstellen, dass kein anderes p4d auf COM1 haengt.");
  Serial.println();

  p4.begin();
}

void loop() {
  bool ok = p4.check();
  Serial.printf("[check  ] %s\n", ok ? "OK - Verbindung steht!" : "keine/ungueltige Antwort");

  char ver[24];
  if (p4.getVersion(ver, sizeof(ver)))
    Serial.printf("[version] Kessel-Software: %s\n", ver);
  else
    Serial.println("[version] keine Antwort");

  Serial.println("---");
  delay(3000);
}
