// ***************************************************************************
// ESP-P4d - COM1-Protokoll (Implementierung), portiert aus horchi/linux-p4d (GPL).
// Siehe P4Com.h und ../../protokoll.md.
// ***************************************************************************
#include "P4Com.h"

#include <string.h>

void P4Com::begin() {
  _s.begin(kBaud, SERIAL_8N1, _rx, _tx);
}

// CRC nach linux-p4d lib/common.c:1804 : XOR ueber (b ^ ((b<<1)&0xFF)).
uint8_t P4Com::crc(const uint8_t* data, int size) {
  uint8_t c = 0;
  for (int i = 0; i < size; i++) {
    uint8_t dummy = (uint8_t)((data[i] << 1) & 0xFF);
    c ^= (uint8_t)(data[i] ^ dummy);
  }
  return c;
}

bool P4Com::readRaw(uint8_t& b, uint32_t timeoutMs) {
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (_s.available()) {
      b = (uint8_t)_s.read();
      if (_rxRawLen < (int)sizeof(_rxRaw)) _rxRaw[_rxRawLen++] = b;
      return true;
    }
    delay(1);
  }
  return false;
}

// Liest ein logisches Byte und hebt das Byte-Stuffing auf (p4io.c:104-160):
//   0x02/0x2B -> gefolgt von 0x00 (verwerfen)
//   0xFE      -> 0x12->0x11, 0x14->0x13, 0x00->0xFE
bool P4Com::readByteDecoded(uint8_t& v, uint32_t timeoutMs) {
  uint8_t b, b1;
  if (!readRaw(b, timeoutMs)) return false;

  if (b != 0x02 && b != 0x2B && b != 0xFE) {
    v = b;
    return true;
  }
  if (b == 0x02 || b == 0x2B) {
    if (!readRaw(b1, timeoutMs) || b1 != 0x00) return false;
    v = b;
    return true;
  }
  // b == 0xFE
  if (!readRaw(b1, timeoutMs)) return false;
  if (b1 == 0x12)      v = 0x11;
  else if (b1 == 0x14) v = 0x13;
  else if (b1 == 0x00) v = 0xFE;
  else return false;
  return true;
}

// Baut den Frame (header+payload+crc), maskiert ab Byte 2 und sendet ihn.
bool P4Com::sendFrame(uint8_t command, const uint8_t* payload, int payloadLen) {
  if (payloadLen < 0 || payloadLen > 254) return false;

  uint8_t tmp[5 + 254 + 1];   // header(5) + payload + crc(1), unmaskiert
  int n = 0;
  uint16_t size = (uint16_t)(payloadLen + 1);   // payload + crc, ohne header

  tmp[n++] = 0x02;                        // id hi (commId 0x02FD, big-endian)
  tmp[n++] = 0xFD;                        // id lo
  tmp[n++] = (uint8_t)(size >> 8);        // size hi
  tmp[n++] = (uint8_t)(size & 0xFF);      // size lo
  tmp[n++] = command;
  for (int i = 0; i < payloadLen; i++) tmp[n++] = payload[i];
  tmp[n] = crc(tmp, n);                   // CRC ueber header+payload
  n++;

  uint8_t out[2 + (255 + 3) * 2];         // id roh + Rest ggf. verdoppelt
  int o = 0;
  out[o++] = tmp[0];                      // id wird NICHT maskiert (Sync)
  out[o++] = tmp[1];
  for (int i = 2; i < n; i++) {
    uint8_t b = tmp[i];
    switch (b) {
      case 0x02: out[o++] = 0x02; out[o++] = 0x00; break;
      case 0x2B: out[o++] = 0x2B; out[o++] = 0x00; break;
      case 0xFE: out[o++] = 0xFE; out[o++] = 0x00; break;
      case 0x11: out[o++] = 0xFE; out[o++] = 0x12; break;
      case 0x13: out[o++] = 0xFE; out[o++] = 0x14; break;
      default:   out[o++] = b; break;
    }
  }

  _s.write(out, o);
  _s.flush();
  if (_hook) _hook(true, command, out, o, true);
  return true;
}

bool P4Com::transact(uint8_t command, const uint8_t* payload, int payloadLen,
                     uint8_t* resp, int respMax, int& respLen, uint32_t timeoutMs) {
  bool ok = transactInner(command, payload, payloadLen, resp, respMax, respLen, timeoutMs);
  if (_hook) _hook(false, command, _rxRaw, _rxRawLen, ok);
  return ok;
}

bool P4Com::transactInner(uint8_t command, const uint8_t* payload, int payloadLen,
                          uint8_t* resp, int respMax, int& respLen, uint32_t timeoutMs) {
  respLen = 0;

  while (_s.available()) _s.read();       // evtl. Restbytes verwerfen

  if (!sendFrame(command, payload, payloadLen)) return false;
  _rxRawLen = 0;

  // Antwort-Header: id (roh, 2 B) muss commId 0x02FD sein
  uint8_t idHi, idLo;
  if (!readRaw(idHi, timeoutMs) || !readRaw(idLo, timeoutMs)) return false;
  if (idHi != 0x02 || idLo != 0xFD) return false;

  uint8_t sHi, sLo, cmd;
  if (!readByteDecoded(sHi, timeoutMs) || !readByteDecoded(sLo, timeoutMs)) return false;
  int size = (sHi << 8) | sLo;            // payload + crc, ohne header
  if (!readByteDecoded(cmd, timeoutMs)) return false;
  if (size < 1 || size > respMax) return false;

  for (int i = 0; i < size; i++) {
    if (!readByteDecoded(resp[i], timeoutMs)) return false;
  }
  respLen = size;

  // CRC pruefen: ueber header(02 FD sHi sLo cmd) + payload(size-1 Bytes)
  uint8_t chk[5 + 254];
  int m = 0;
  chk[m++] = 0x02; chk[m++] = 0xFD; chk[m++] = sHi; chk[m++] = sLo; chk[m++] = cmd;
  for (int i = 0; i < size - 1; i++) chk[m++] = resp[i];
  if (crc(chk, m) != resp[size - 1]) return false;

  return true;
}

bool P4Com::check() {
  const char* t = "Tescht ;-)";          // wie in linux-p4d p4io.c:453
  uint8_t resp[264];
  int len;
  return transact(cmdCheck, (const uint8_t*)t, (int)strlen(t), resp, sizeof(resp), len);
}

bool P4Com::getVersion(char* out, size_t outSize) {
  uint8_t resp[264];
  int len;
  if (!transact(cmdGetVersion, nullptr, 0, resp, sizeof(resp), len)) return false;
  if (len < 5) return false;              // 4 Versionsbytes + mind. CRC
  snprintf(out, outSize, "%02x.%02x.%02x.%02x", resp[0], resp[1], resp[2], resp[3]);
  return true;
}

bool P4Com::getValue(uint16_t address, int16_t& value) {
  uint8_t payload[2] = { (uint8_t)(address >> 8), (uint8_t)(address & 0xFF) };
  uint8_t resp[264];
  int len;
  if (!transact(cmdGetValue, payload, 2, resp, sizeof(resp), len)) return false;
  // p4io.c getValue: Antwort = value(2) + crc(1). Liefert ein Kessel zusaetzlich
  // die Adresse davor (address(2) + value(2) + crc), wird sie uebersprungen.
  if (len == 3) value = (int16_t)((resp[0] << 8) | resp[1]);
  else if (len >= 5) value = (int16_t)((resp[2] << 8) | resp[3]);
  else return false;
  return true;
}

void P4Com::latin1ToUtf8(const uint8_t* in, int len, char* out, size_t outSize) {
  size_t o = 0;
  for (int i = 0; i < len && o + 1 < outSize; i++) {
    uint8_t c = in[i];
    if (c == 0x00 || c == 0x01 || c == 0x02 || c == 0x03) continue;
    if (c < 0x80) {
      out[o++] = (char)c;
    } else {
      if (o + 2 >= outSize) break;
      out[o++] = (char)(0xC0 | (c >> 6));
      out[o++] = (char)(0x80 | (c & 0x3F));
    }
  }
  out[o] = 0;
  while (o > 0 && out[o - 1] == ' ') out[--o] = 0;  // wie allTrim()
  size_t lead = 0;
  while (out[lead] == ' ') lead++;
  if (lead) memmove(out, out + lead, strlen(out + lead) + 1);
}

// p4io.c getStatus: mode(1) state(1) text "Betriebsart;Zustand" crc(1)
bool P4Com::getState(uint8_t& mode, uint8_t& state, char* modeText, size_t modeSize, char* stateText,
                     size_t stateSize) {
  uint8_t resp[264];
  int len;
  if (!transact(cmdGetState, nullptr, 0, resp, sizeof(resp), len)) return false;
  if (len < 3) return false;
  mode = resp[0];
  state = resp[1];
  char text[160];
  latin1ToUtf8(resp + 2, len - 3, text, sizeof(text));
  char* sep = strchr(text, ';');
  if (!sep) return false;  // p4d: "Communication error"
  *sep = 0;
  // Kessel kuerzt auf "\xC3\x9Cbergangsbetr" (p4io.c ersetzt das ebenso)
  strlcpy(modeText, strcmp(text, "\xC3\x9C" "bergangsbetr") == 0 ? "\xC3\x9C" "bergangsbetrieb" : text, modeSize);
  strlcpy(stateText, sep + 1, stateSize);
  return true;
}

// p4io.c getValueSpec: more(1); bei more: factor(2) type(2) unit(2) address(2)
// description(n) termination(1) crc(1). Kurze Eintraege (< 11 Byte) = leer.
P4Com::ListResult P4Com::getValueSpec(bool first, ValueSpec& v) {
  uint8_t resp[264];
  int len;
  if (!transact(first ? cmdGetValueListFirst : cmdGetValueListNext, nullptr, 0, resp, sizeof(resp), len))
    return listFail;
  if (len < 2) return listFail;
  if (!resp[0]) return listEnd;
  if (len < 11) return listSkip;
  v.factor = (resp[1] << 8) | resp[2];
  if (v.factor == 0) v.factor = 1;
  v.type = (resp[3] << 8) | resp[4];
  latin1ToUtf8(resp + 5, 2, v.unit, sizeof(v.unit));
  v.address = (resp[7] << 8) | resp[8];
  int descLen = len - 9 - 2;  // ohne termination + crc
  latin1ToUtf8(resp + 9, descLen > 0 ? descLen : 0, v.description, sizeof(v.description));
  if (!v.unit[0]) {
    // Einheit notfalls aus der Beschreibung "... [°C]" (wie p4io.c)
    const char* a = strchr(v.description, '[');
    const char* b = a ? strchr(a, ']') : nullptr;
    if (a && b && b - a - 1 > 0 && (size_t)(b - a - 1) < sizeof(v.unit)) {
      memcpy(v.unit, a + 1, b - a - 1);
      v.unit[b - a - 1] = 0;
    }
  }
  return listOk;
}

// p4io.c getError: more(1); bei more: number(2) info(1) state(1)
// time s,m,h(3) date d,m,y(3) text(n) crc(1)
P4Com::ListResult P4Com::getError(bool first, ErrorInfo& e) {
  uint8_t resp[264];
  int len;
  if (!transact(first ? cmdGetErrorFirst : cmdGetErrorNext, nullptr, 0, resp, sizeof(resp), len)) return listFail;
  if (len < 2) return listFail;
  if (!resp[0]) return listEnd;
  if (len < 12) return listSkip;
  e.number = (resp[1] << 8) | resp[2];
  e.info = resp[3];
  e.state = resp[4];
  snprintf(e.time, sizeof(e.time), "%02u.%02u.%02u %02u:%02u:%02u", resp[8], resp[9], resp[10], resp[7], resp[6],
           resp[5]);
  latin1ToUtf8(resp + 11, len - 12, e.text, sizeof(e.text));
  return listOk;
}
