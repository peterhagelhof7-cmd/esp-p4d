#include "Log.h"

#include <stdarg.h>

namespace {
constexpr int kLines = 60;
constexpr int kLineLen = 120;
char g_buf[kLines][kLineLen];
int g_next = 0;
int g_count = 0;
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
}  // namespace

namespace Log {

static unsigned long g_bootMs = 0;

void markBoot() { g_bootMs = millis(); }
unsigned long uptimeMs() { return millis() - g_bootMs; }

void add(const char* fmt, ...) {
  char msg[kLineLen - 16];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);

  unsigned long s = uptimeMs() / 1000;
  char line[kLineLen];
  snprintf(line, sizeof(line), "[%3lu:%02lu:%02lu] %s", s / 3600, (s / 60) % 60, s % 60, msg);
  Serial.println(line);

  portENTER_CRITICAL(&g_mux);
  strlcpy(g_buf[g_next], line, kLineLen);
  g_next = (g_next + 1) % kLines;
  if (g_count < kLines) g_count++;
  portEXIT_CRITICAL(&g_mux);
}

String text() {
  String out;
  out.reserve(g_count * 60);
  portENTER_CRITICAL(&g_mux);
  int start = (g_next - g_count + kLines) % kLines;
  int n = g_count;
  portEXIT_CRITICAL(&g_mux);
  for (int i = 0; i < n; i++) {
    out += g_buf[(start + i) % kLines];
    out += '\n';
  }
  return out;
}

}  // namespace Log
