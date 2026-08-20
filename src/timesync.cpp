#include "timesync.h"
#include <Arduino.h>
#include <time.h>

namespace {
    bool synced = false;
}

namespace timesync {

void begin() {
    // POSIX TZ string handles French DST transitions automatically.
    // Change this if you're building this hardware somewhere else.
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");

    struct tm timeinfo;
    synced = getLocalTime(&timeinfo, 4000); // up to 4s, once, at boot
}

bool isSynced() {
    if (synced) return true;
    struct tm timeinfo;
    synced = getLocalTime(&timeinfo, 10);
    return synced;
}

int hour() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10)) return -1;
    return timeinfo.tm_hour;
}

float hourFraction() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10)) return -1.0f;
    return timeinfo.tm_hour + timeinfo.tm_min / 60.0f;
}

uint32_t epoch() {
    if (!isSynced()) return 0;
    time_t now;
    time(&now);
    return (uint32_t)now;
}

}
