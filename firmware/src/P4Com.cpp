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
  return true;
}

bool P4Com::transact(uint8_t command, const uint8_t* payload, int payloadLen,
                     uint8_t* resp, int respMax, int& respLen, uint32_t timeoutMs) {
  respLen = 0;

  while (_s.available()) _s.read();       // evtl. Restbytes verwerfen

  if (!sendFrame(command, payload, payloadLen)) return false;

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
  if (len < 5) return false;              // address(2) + value(2) + crc(1)
  value = (int16_t)((resp[2] << 8) | resp[3]);
  return true;
}
