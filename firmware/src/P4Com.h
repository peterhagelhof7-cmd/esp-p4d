// ***************************************************************************
// ESP-P4d - Fröling P4 / Lambdatronic S3200 COM1-Service-Protokoll
//
// Read-only-Teilmenge, portiert aus horchi/linux-p4d (p4io.c/service.h/
// lib/common.c) - siehe ../../protokoll.md. Weil aus GPL-Code abgeleitet, steht
// dieses Modul (und damit die Firmware) unter der GPL.
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
    cmdCheck      = 0x22,   // Verbindung pruefen
    cmdGetValue   = 0x30,   // aktuellen Wert (per Adresse) lesen
    cmdGetVersion = 0x41,   // SW-Version + Zeit/Datum
    cmdGetState   = 0x51,   // Kesselzustand
  };

  // rxPin = MAX3232 R1OUT (Kessel-TX), txPin -> MAX3232 T1IN (Kessel-RX).
  P4Com(HardwareSerial& serial, int8_t rxPin, int8_t txPin)
      : _s(serial), _rx(rxPin), _tx(txPin) {}

  void begin();

  // --- Proof-of-Life -------------------------------------------------------
  bool check();                                // gueltige Antwort auf cmdCheck?
  bool getVersion(char* out, size_t outSize);  // -> "xx.xx.xx.xx"

  // Einzelwert lesen (roh, signed 16-bit; reale Groesse = value / factor, wobei
  // factor/Einheit/Name aus der ValueList kommen - kommt in einem spaeteren Modul).
  bool getValue(uint16_t address, int16_t& value);

 private:
  HardwareSerial& _s;
  int8_t _rx;
  int8_t _tx;

  static uint8_t crc(const uint8_t* data, int size);

  bool readRaw(uint8_t& b, uint32_t timeoutMs);
  bool readByteDecoded(uint8_t& v, uint32_t timeoutMs);
  bool sendFrame(uint8_t command, const uint8_t* payload, int payloadLen);

  // Sendet command+payload, liest Antwort-Header (id muss 0x02FD sein) + die
  // 'size' dekodierten Nutzbytes (payload inkl. CRC) nach resp und prueft die CRC.
  bool transact(uint8_t command, const uint8_t* payload, int payloadLen,
                uint8_t* resp, int respMax, int& respLen, uint32_t timeoutMs = 2000);
};
