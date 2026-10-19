#include "KesselManager.h"

#include <algorithm>

#include "Log.h"

namespace {
constexpr unsigned long kCheckEveryMs = 10000;       // ohne Verbindung
constexpr unsigned long kStateEveryMs = 10000;
constexpr unsigned long kErrorsEveryMs = 5UL * 60UL * 1000UL;
constexpr unsigned long kValueSpacingMs = 50;        // Pause zwischen Einzelwerten
constexpr uint8_t kMaxFailures = 3;
constexpr size_t kMaxErrors = 20;
constexpr size_t kMaxValues = 400;                   // Schutz gegen endlose Listen
}  // namespace

void KesselManager::begin() {}

void KesselManager::result(bool ok) {
  _polls++;
  if (ok) {
    _failures = 0;
    _lastOkMs = millis();
    return;
  }
  _errorsCount++;
  if (_connected && ++_failures >= kMaxFailures) {
    std::lock_guard<std::mutex> lock(_mu);
    _connected = false;
    _catalog = _values.empty() ? catNone : catReady;  // Katalog behalten, Neuaufbau nur auf Wunsch
    _valuesNew.clear();
    _errorsLoading = false;
    Log::add("Heizung antwortet nicht mehr auf COM1");
  }
}

void KesselManager::loop() {
  unsigned long now = millis();

  if (!_connected) {
    if (_lastCheckMs && now - _lastCheckMs < kCheckEveryMs) return;
    _lastCheckMs = now;
    if (!_p4.check()) {
      _polls++;
      _errorsCount++;
      return;
    }
    char ver[24] = "";
    _p4.getVersion(ver, sizeof(ver));
    {
      std::lock_guard<std::mutex> lock(_mu);
      _connected = true;
      _failures = 0;
      _lastOkMs = now;
      if (ver[0]) strlcpy(_version, ver, sizeof(_version));
    }
    _polls++;
    _stateMs = _errorsMs = 0;
    Log::add("Heizung antwortet auf COM1 (Software %s)", _version);
    return;
  }

  if (_rescanRequested || _catalog == catNone) {
    _rescanRequested = false;
    _catalog = catLoading;
    _valuesNew.clear();
    Log::add("Lese Werte-Katalog der Heizung ...");
  }
  if (_catalog == catLoading) {
    stepCatalog();
    return;
  }
  if (!_stateMs || now - _stateMs >= kStateEveryMs) {
    _stateMs = now;
    uint8_t mode, state;
    char m[40], s[64];
    bool ok = _p4.getState(mode, state, m, sizeof(m), s, sizeof(s));
    if (ok) {
      std::lock_guard<std::mutex> lock(_mu);
      if (strcmp(s, _stateText) != 0) Log::add("Kessel: %s / %s", m, s);
      _mode = mode;
      _state = state;
      strlcpy(_modeText, m, sizeof(_modeText));
      strlcpy(_stateText, s, sizeof(_stateText));
    }
    result(ok);
    return;
  }
  if (_errorsLoading || !_errorsMs || now - _errorsMs >= kErrorsEveryMs) {
    if (!_errorsLoading) {
      _errorsMs = now;
      _errorsLoading = true;
      _errorsNew.clear();
    }
    stepErrors();
    return;
  }
  stepValue();
}

bool KesselManager::stepCatalog() {
  P4Com::ValueSpec spec;
  P4Com::ListResult r = _p4.getValueSpec(_valuesNew.empty(), spec);
  result(r != P4Com::listFail);
  if (r == P4Com::listFail) {
    // Abbruch mitten im Katalog: beim naechsten Mal von vorn
    if (!_connected) return false;
    _valuesNew.clear();
    return false;
  }
  if (r == P4Com::listOk) {
    Value v;
    v.spec = spec;
    _valuesNew.push_back(v);
    // leere Eintraege (listSkip) zaehlen nicht, die Liste geht trotzdem weiter
    if (_valuesNew.size() < kMaxValues) return true;
  } else if (r == P4Com::listSkip) {
    if (_valuesNew.empty()) {
      // erster Eintrag leer: als Platzhalter merken, damit "Next" folgt
      Value v;
      v.spec.address = 0xFFFF;
      _valuesNew.push_back(v);
    }
    return true;
  }
  // Ende der Liste (oder Obergrenze)
  _valuesNew.erase(std::remove_if(_valuesNew.begin(), _valuesNew.end(),
                                  [](const Value& v) { return v.spec.address == 0xFFFF; }),
                   _valuesNew.end());
  {
    std::lock_guard<std::mutex> lock(_mu);
    _values.swap(_valuesNew);
    _valuesNew.clear();
    _catalog = catReady;
    _nextValue = 0;
  }
  Log::add("Werte-Katalog: %u Werte", (unsigned)_values.size());
  return true;
}

bool KesselManager::stepErrors() {
  P4Com::ErrorInfo e;
  P4Com::ListResult r = _p4.getError(_errorsNew.empty(), e);
  result(r != P4Com::listFail);
  if (r == P4Com::listFail) {
    _errorsLoading = false;
    return false;
  }
  if (r == P4Com::listOk && _errorsNew.size() < kMaxErrors) {
    _errorsNew.push_back(e);
    return true;
  }
  if (r == P4Com::listSkip) return true;
  std::lock_guard<std::mutex> lock(_mu);
  _errors.swap(_errorsNew);
  _errorsNew.clear();
  _errorsLoading = false;
  return true;
}

bool KesselManager::stepValue() {
  static unsigned long lastMs = 0;
  if (_values.empty() || millis() - lastMs < kValueSpacingMs) return false;
  lastMs = millis();
  if (_nextValue >= _values.size()) _nextValue = 0;
  uint16_t addr = _values[_nextValue].spec.address;
  int16_t raw;
  bool ok = _p4.getValue(addr, raw);
  if (ok) {
    std::lock_guard<std::mutex> lock(_mu);
    Value& v = _values[_nextValue];
    v.raw = raw;
    v.valid = true;
    v.readMs = millis();
  }
  _nextValue++;
  result(ok);
  return ok;
}

// real = roh / Faktor, Nachkommastellen passend zum Faktor (10 -> 1, 100 -> 2)
void KesselManager::appendNumber(String& out, const Value& v) {
  if (!v.valid) {
    out += "null";
    return;
  }
  uint16_t f = v.spec.factor ? v.spec.factor : 1;
  int digits = f >= 100 ? 2 : (f > 1 ? 1 : 0);
  char buf[24];
  snprintf(buf, sizeof(buf), "%.*f", digits, (double)v.raw / f);
  out += buf;
}

void KesselManager::appendEscaped(String& out, const char* s) {
  out += '"';
  for (; *s; s++) {
    char c = *s;
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if ((uint8_t)c < 0x20) {
      out += ' ';
    } else {
      out += c;
    }
  }
  out += '"';
}

String KesselManager::statusJson() {
  std::lock_guard<std::mutex> lock(_mu);
  String o = "{\"connected\":";
  o += _connected ? "true" : "false";
  o += ",\"version\":";
  appendEscaped(o, _version);
  o += ",\"lastOkAgo\":";
  o += _lastOkMs ? String((millis() - _lastOkMs) / 1000) : String("-1");
  o += ",\"polls\":";
  o += _polls;
  o += ",\"failures\":";
  o += _errorsCount;
  o += ",\"mode\":";
  appendEscaped(o, _modeText);
  o += ",\"state\":";
  appendEscaped(o, _stateText);
  o += ",\"catalog\":\"";
  o += _catalog == catReady ? "ready" : (_catalog == catLoading ? "loading" : "none");
  o += "\",\"catalogLoaded\":";
  o += _catalog == catLoading ? _valuesNew.size() : _values.size();
  o += ",\"errors\":[";
  for (size_t i = 0; i < _errors.size(); i++) {
    const P4Com::ErrorInfo& e = _errors[i];
    o += i ? ",{" : "{";
    o += "\"nr\":";
    o += e.number;
    o += ",\"state\":";
    o += e.state;
    o += ",\"time\":";
    appendEscaped(o, e.time);
    o += ",\"text\":";
    appendEscaped(o, e.text);
    o += "}";
  }
  o += "]}";
  return o;
}

String KesselManager::valuesJson(const std::vector<uint16_t>& selected) {
  std::lock_guard<std::mutex> lock(_mu);
  String o;
  o.reserve(_values.size() * 110 + 64);
  o += "{\"connected\":";
  o += _connected ? "true" : "false";
  o += ",\"values\":[";
  unsigned long now = millis();
  for (size_t i = 0; i < _values.size(); i++) {
    const Value& v = _values[i];
    char k[8];
    snprintf(k, sizeof(k), "0x%04X", v.spec.address);
    o += i ? ",{\"k\":\"" : "{\"k\":\"";
    o += k;
    o += "\",\"n\":";
    appendEscaped(o, v.spec.description);
    o += ",\"v\":";
    appendNumber(o, v);
    o += ",\"u\":";
    appendEscaped(o, v.spec.unit);
    o += ",\"f\":";
    o += v.spec.factor;
    o += ",\"t\":";
    o += v.spec.type;
    o += ",\"raw\":";
    o += v.raw;
    o += ",\"age\":";
    o += v.valid ? String((now - v.readMs) / 1000) : String("null");
    o += ",\"sel\":";
    o += std::find(selected.begin(), selected.end(), v.spec.address) != selected.end() ? "true" : "false";
    o += "}";
  }
  o += "]}";
  return o;
}

String KesselManager::summaryJson(const std::vector<uint16_t>& selected, const String& name, time_t ts) {
  std::lock_guard<std::mutex> lock(_mu);
  String o;
  o.reserve(512 + selected.size() * 90);
  o += "{\"ts\":";
  o += ts ? String((unsigned long)ts) : String("null");
  o += ",\"name\":";
  appendEscaped(o, name.c_str());
  o += ",\"online\":";
  o += _connected ? "true" : "false";
  o += ",\"mode\":";
  appendEscaped(o, _modeText);
  o += ",\"state\":";
  appendEscaped(o, _stateText);
  // Annahme (am Kessel noch zu pruefen): erster Eintrag des Fehlerpuffers ist
  // der neueste; state 1 = gekommen = Stoerung steht an.
  o += ",\"error\":";
  if (!_errors.empty() && _errors[0].state == 1) appendEscaped(o, _errors[0].text);
  else o += "null";
  o += ",\"values\":[";
  bool first = true;
  for (const Value& v : _values) {
    if (!selected.empty() && std::find(selected.begin(), selected.end(), v.spec.address) == selected.end()) continue;
    char k[8];
    snprintf(k, sizeof(k), "0x%04X", v.spec.address);
    o += first ? "{\"k\":\"" : ",{\"k\":\"";
    first = false;
    o += k;
    o += "\",\"n\":";
    appendEscaped(o, v.spec.description);
    o += ",\"v\":";
    // veraltete Werte (keine Verbindung) als null, damit niemand alte Zahlen anzeigt
    if (_connected) appendNumber(o, v);
    else o += "null";
    o += ",\"u\":";
    appendEscaped(o, v.spec.unit);
    o += "}";
  }
  o += "]}";
  return o;
}
