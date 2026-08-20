#pragma once
#include <cstdint>

// NTP time sync (Europe/Paris, DST-aware). Call begin() once after WiFi
// connects. Everything else degrades gracefully (isSynced() false, hour()
// -1) if sync never happens — e.g. offline / captive-portal-abandoned runs.
namespace timesync {

void begin();
bool isSynced();
int hour();          // 0-23 local time, -1 if not synced
float hourFraction(); // hour + minute/60, for smooth interpolation; -1 if not synced
uint32_t epoch();     // unix seconds, 0 if not synced

}
