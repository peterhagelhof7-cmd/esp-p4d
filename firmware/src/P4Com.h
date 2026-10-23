// ***************************************************************************
// ESP-P4d - Fröling P4 / Lambdatronic S3200 COM1-Service-Protokoll
//
// Read-only-Teilmenge, portiert aus horchi/linux-p4d (p4io.c/service.h/
// lib/common.c) - siehe ../../docs/protokoll.md. Weil aus GPL-2.0-Code abgeleitet, steht
// dieses Modul (und damit die Firmware) unter der GPL-2.0 (wie linux-p4d).
//
// Protokoll kompakt: 57600 8N1, Frame = id(2)=0x02FD + size(2,BE) + command(1)
// + payload + crc(1). id roh (Sync), Rest byte-gestufft; CRC = XOR ueber
// header+payload (unmaskiert). Synchron: Request senden -> Antwort lesen.
// ***************************************************************************
#pragma once

#include <Arduino.h>

class P4Com {
 public:
  static const uint16_t kCommId = 0x02FD;   // Rahmen-Startkennung / commId
  static const uint32_t kBaud   = 57600;    // COM1: 57600 8N1

  enum Command : uint8_t {
    cmdCheck             = 0x22,   // Verbindung pruefen
    cmdGetValue          = 0x30,   // aktuellen Wert (per Adresse) lesen
    cmdGetValueListFirst = 0x31,   // Werte-Katalog (ValueSpec), erster Eintrag
    cmdGetValueListNext  = 0x32,   // ... naechster Eintrag
    cmdGetVersion        = 0x41,   // SW-Version + Zeit/Datum
    cmdGetErrorFirst     = 0x47,   // Fehlerpuffer, erster Eintrag
    cmdGetErrorNext      = 0x48,   // ... naechster Eintrag
    cmdGetState          = 0x51,   // Kesselzustand
  };

  // Ein Eintrag des Werte-Katalogs (p4io.c getValueSpec). Texte als UTF-8.
  struct ValueSpec {
    uint16_t address = 0;
    uint16_t factor = 1;
    uint16_t type = 0;
    char unit[8] = "";
    char description[48] = "";
  };

  struct ErrorInfo {
    uint16_t number = 0;
    uint8_t info = 0;
    uint8_t state = 0;     // 1 = gekommen, 2 = quittiert, 4 = gegangen (am Kessel gesehen: 1/2/4)
    char time[20] = "";    // "TT.MM.JJ hh:mm:ss" wie vom Kessel geliefert
    uint32_t sortKey = 0;  // Zeitpunkt als Zahl (Sekunden seit 2000, grob) zum Sortieren
    char text[48] = "";
  };

  enum ListResult { listOk, listEnd, listSkip, listFail };

  // Mitschnitt jedes Rahmens fuer die Webkonsole: tx = gesendet, raw = Bytes so
  // wie auf der Leitung (mit Byte-Stuffing), ok = Antwort gueltig (bei rx).
  typedef void (*TrafficHook)(bool tx, uint8_t command, const uint8_t* raw, int len, bool ok);
  void setTrafficHook(TrafficHook hook) { _hook = hook; }

  // rxPin = MAX3232 R1OUT (Kessel-TX), txPin -> MAX3232 T1IN (Kessel-RX).
  P4Com(HardwareSerial& serial, int8_t rxPin, int8_t txPin)
      : _s(serial), _rx(rxPin), _tx(txPin) {}

  void begin();

  bool check();                                // gueltige Antwort auf cmdCheck?
  bool getVersion(char* out, size_t outSize);  // -> "xx.xx.xx.xx"

  // Einzelwert lesen (roh, signed 16-bit; real = value / factor aus dem Katalog).
  bool getValue(uint16_t address, int16_t& value);

  // Betriebsart + Zustand ("Automatik" / "Heizen") samt Codes.
  bool getState(uint8_t& mode, uint8_t& state, char* modeText, size_t modeSize, char* stateText, size_t stateSize);

  ListResult getValueSpec(bool first, ValueSpec& v);
  ListResult getError(bool first, ErrorInfo& e);

 private:
  HardwareSerial& _s;
  int8_t _rx;
  int8_t _tx;
  TrafficHook _hook = nullptr;

  // Rohbytes der laufenden Antwort fuer den Mitschnitt
  uint8_t _rxRaw[160];
  int _rxRawLen = 0;

  static uint8_t crc(const uint8_t* data, int size);

  bool readRaw(uint8_t& b, uint32_t timeoutMs);
  bool readByteDecoded(uint8_t& v, uint32_t timeoutMs);
  bool sendFrame(uint8_t command, const uint8_t* payload, int payloadLen);

  // Sendet command+payload, liest Antwort-Header (id muss 0x02FD sein) + die
  // 'size' dekodierten Nutzbytes (payload inkl. CRC) nach resp und prueft die CRC.
  bool transact(uint8_t command, const uint8_t* payload, int payloadLen,
                uint8_t* resp, int respMax, int& respLen, uint32_t timeoutMs = 2000);
  bool transactInner(uint8_t command, const uint8_t* payload, int payloadLen,
                     uint8_t* resp, int respMax, int& respLen, uint32_t timeoutMs);

 public:
  // Kessel-Texte sind ISO-8859-1; Steuerzeichen 0x01-0x03 entfallen (p4io.c readText).
  static void latin1ToUtf8(const uint8_t* in, int len, char* out, size_t outSize);
};
