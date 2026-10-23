#pragma once

#include <Arduino.h>

#include <mutex>
#include <vector>

#include "P4Com.h"

// Fragt die Heizung zyklisch ueber COM1 ab - pro loop()-Aufruf hoechstens EINE
// Transaktion (max. ca. 2 s), damit Watchdog und Netz nicht leiden:
//  - ohne Verbindung: alle 10 s cmdCheck; danach Version + Werte-Katalog
//    (0x31/0x32: welche Werte der Kessel kennt, mit Name/Einheit/Faktor)
//  - Zustand (0x51) alle 10 s, Fehlerpuffer (0x47/0x48) alle 5 min (ganz
//    gelesen, die 20 neuesten bleiben, neueste zuerst)
//  - dazwischen reihum alle Katalog-Werte (0x30), real = roh / Faktor
//  - 3 Fehlschlaege in Folge -> "keine Verbindung", Werte gelten als veraltet
// Die Weboberflaeche liest ueber die *Json()-Funktionen (mit Mutex).
class KesselManager {
 public:
  explicit KesselManager(P4Com& p4) : _p4(p4) {}

  void begin();
  void loop();
  void requestRescan() { _rescanRequested = true; }

  bool connected() const { return _connected; }

  // Teil von /api/status
  String statusJson();
  // Alle Werte; selected = Adressen fuer die Zusammenfassung (Haekchen).
  String valuesJson(const std::vector<uint16_t>& selected);
  // Fuer home-central: nur ausgewaehlte Werte (leer = alle), ts = Unix-Zeit oder 0.
  String summaryJson(const std::vector<uint16_t>& selected, const String& name, time_t ts);

 private:
  struct Value {
    P4Com::ValueSpec spec;
    int16_t raw = 0;
    bool valid = false;
    unsigned long readMs = 0;
  };

  P4Com& _p4;
  std::mutex _mu;

  bool _connected = false;
  uint8_t _failures = 0;
  uint32_t _polls = 0;
  uint32_t _errorsCount = 0;
  unsigned long _lastOkMs = 0;
  char _version[24] = "";

  uint8_t _mode = 0, _state = 0;
  char _modeText[40] = "";
  char _stateText[64] = "";
  unsigned long _stateMs = 0;

  std::vector<P4Com::ErrorInfo> _errors, _errorsNew;
  bool _errorsLoading = false;
  unsigned long _errorsMs = 0;
  uint16_t _errorReads = 0;  // Eintraege im laufenden Durchgang

  std::vector<Value> _values, _valuesNew;  // _valuesNew = Katalog im Aufbau
  enum CatalogState { catNone, catLoading, catReady } _catalog = catNone;
  bool _rescanRequested = false;
  size_t _nextValue = 0;
  // Vitalwerte COM1: Dauer und Fehler der letzten vollstaendigen Werte-Runde
  unsigned long _roundStartMs = 0, _roundMs = 0;
  uint32_t _roundPolls = 0, _roundErrors = 0, _lastRoundPolls = 0, _lastRoundErrors = 0;

  unsigned long _lastCheckMs = 0;

  void result(bool ok);
  bool stepCatalog();
  bool stepErrors();
  bool stepValue();
  const P4Com::ErrorInfo* activeError() const;  // _mu muss gehalten sein
  static bool sensorMissing(const Value& v);
  static void appendNumber(String& out, const Value& v);
  static void appendEscaped(String& out, const char* s);
};
