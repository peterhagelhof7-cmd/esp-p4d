#include "ComMonitor.h"

#include <mutex>

#include "Log.h"

namespace {
constexpr int kEntries = 120;
constexpr int kMaxBytes = 64;  // laengere Rahmen werden gekuerzt angezeigt

struct Entry {
  uint32_t seq;
  uint32_t ms;
  bool tx;
  bool ok;
  uint8_t command;
  uint8_t len;      // gespeicherte Bytes
  uint16_t total;   // tatsaechliche Laenge
  uint8_t data[kMaxBytes];
};

Entry g_buf[kEntries];
uint32_t g_nextSeq = 1;
std::mutex g_mu;

const char* commandName(uint8_t c) {
  switch (c) {
    case 0x22: return "Check";
    case 0x30: return "GetValue";
    case 0x31: return "ValueListFirst";
    case 0x32: return "ValueListNext";
    case 0x41: return "GetVersion";
    case 0x47: return "ErrorFirst";
    case 0x48: return "ErrorNext";
    case 0x51: return "GetState";
    default: return "?";
  }
}
}  // namespace

namespace ComMonitor {

void record(bool tx, uint8_t command, const uint8_t* raw, int len, bool ok) {
  std::lock_guard<std::mutex> lock(g_mu);
  Entry& e = g_buf[g_nextSeq % kEntries];
  e.seq = g_nextSeq++;
  e.ms = Log::uptimeMs();
  e.tx = tx;
  e.ok = ok;
  e.command = command;
  e.total = len < 0 ? 0 : len;
  e.len = e.total > kMaxBytes ? kMaxBytes : e.total;
  memcpy(e.data, raw, e.len);
}

String json(uint32_t since) {
  std::lock_guard<std::mutex> lock(g_mu);
  uint32_t oldest = g_nextSeq > kEntries ? g_nextSeq - kEntries : 1;
  if (since < oldest) since = oldest;
  String out;
  out.reserve(256 + (g_nextSeq - since) * 120);
  out += "{\"next\":";
  out += g_nextSeq;
  out += ",\"lines\":[";
  char hex[kMaxBytes * 3 + 8];
  for (uint32_t s = since; s < g_nextSeq; s++) {
    const Entry& e = g_buf[s % kEntries];
    int h = 0;
    hex[0] = 0;  // leere Antwort: sonst stuende hier noch der vorige Rahmen
    for (int i = 0; i < e.len; i++) h += snprintf(hex + h, sizeof(hex) - h, i ? " %02X" : "%02X", e.data[i]);
    if (e.total > e.len) snprintf(hex + h, sizeof(hex) - h, " ...");
    char line[sizeof(hex) + 128];
    snprintf(line, sizeof(line), "%s{\"s\":%lu,\"t\":%lu,\"d\":\"%s\",\"c\":\"0x%02X\",\"n\":\"%s\",\"b\":%u,\"h\":\"%s\",\"ok\":%s}",
             s == since ? "" : ",", (unsigned long)e.seq, (unsigned long)e.ms, e.tx ? "tx" : "rx", e.command,
             commandName(e.command), e.total, hex, e.ok ? "true" : "false");
    out += line;
  }
  out += "]}";
  return out;
}

}  // namespace ComMonitor
