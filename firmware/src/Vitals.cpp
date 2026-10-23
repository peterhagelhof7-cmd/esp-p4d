#include "Vitals.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_sntp.h>
#include <esp_system.h>

#include "Log.h"

namespace {
constexpr unsigned long kSampleEveryMs = 30000;  // Temperatur, Stack, Dateisystem

esp_reset_reason_t g_reason = ESP_RST_UNKNOWN;
uint32_t g_boots = 0;
float g_chipTemp = NAN;
uint32_t g_stackFree = 0;  // Bytes Reserve im Loop-Task (kleinster Wert seit Start)
size_t g_fsUsed = 0, g_fsTotal = 0;
unsigned long g_loopMaxMs = 0;
unsigned long g_lastLoopMs = 0, g_lastSampleMs = 0;
volatile unsigned long g_ntpSyncMs = 0;  // 0 = noch nie

void onTimeSync(struct timeval*) { g_ntpSyncMs = millis() ? millis() : 1; }

void sample() {
  // temperatureRead() startet/stoppt den internen Sensor jedes Mal (S2: ca. +-3 °C)
  g_chipTemp = temperatureRead();
  g_stackFree = uxTaskGetStackHighWaterMark(NULL);  // Arduino-ESP32: in Bytes
  g_fsUsed = LittleFS.usedBytes();
  g_fsTotal = LittleFS.totalBytes();
}
}  // namespace

namespace Vitals {

const char* resetReason() {
  switch (g_reason) {
    case ESP_RST_POWERON: return "poweron";
    case ESP_RST_EXT: return "ext";
    case ESP_RST_SW: return "sw";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int_wdt";
    case ESP_RST_TASK_WDT: return "task_wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    default: return "unknown";
  }
}

bool abnormalReset() {
  return g_reason == ESP_RST_PANIC || g_reason == ESP_RST_INT_WDT || g_reason == ESP_RST_TASK_WDT ||
         g_reason == ESP_RST_WDT || g_reason == ESP_RST_BROWNOUT;
}

uint32_t bootCount() { return g_boots; }

void begin() {
  g_reason = esp_reset_reason();
  Preferences p;
  if (p.begin("vital", false)) {
    g_boots = p.getUInt("boots", 0) + 1;
    p.putUInt("boots", g_boots);  // ein NVS-Schreibvorgang je Start
    p.end();
  }
  sntp_set_time_sync_notification_cb(onTimeSync);
  if (abnormalReset()) Log::add("WARNUNG: Neustart nach %s (Start Nr. %u)", resetReason(), (unsigned)g_boots);
  else Log::add("Neustart-Grund: %s (Start Nr. %u)", resetReason(), (unsigned)g_boots);
  sample();
}

void loop() {
  unsigned long now = millis();
  // Abstand zwischen zwei Aufrufen = Dauer eines loop()-Durchlaufs (inkl. delay(2))
  if (g_lastLoopMs && now - g_lastLoopMs > g_loopMaxMs) g_loopMaxMs = now - g_lastLoopMs;
  g_lastLoopMs = now;
  if (now - g_lastSampleMs >= kSampleEveryMs) {
    g_lastSampleMs = now;
    sample();
  }
}

void toJson(JsonObject o) {
  if (!isnan(g_chipTemp)) o["chipTemp"] = roundf(g_chipTemp * 10) / 10;
  else o["chipTemp"] = nullptr;
  o["resetReason"] = resetReason();
  o["abnormalReset"] = abnormalReset();
  o["boots"] = g_boots;
  o["heapFree"] = ESP.getFreeHeap();
  o["heapMin"] = ESP.getMinFreeHeap();
  // nur interner RAM (MALLOC_CAP_8BIT allein zaehlt das PSRAM mit -> 2 MB)
  o["heapMaxBlock"] = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  o["psramSize"] = ESP.getPsramSize();
  o["psramFree"] = ESP.getFreePsram();
  o["loopMaxMs"] = g_loopMaxMs;
  o["stackFree"] = g_stackFree;
  o["fsUsed"] = g_fsUsed;
  o["fsTotal"] = g_fsTotal;
  unsigned long sync = g_ntpSyncMs;
  if (sync) o["ntpSyncAgo"] = (millis() - sync) / 1000;
  else o["ntpSyncAgo"] = nullptr;
}

}  // namespace Vitals
