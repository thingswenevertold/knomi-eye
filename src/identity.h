#pragma once

// Per-device hostname/passwords, "sticky" across firmware flashes.
//
// On a device's first-ever boot, these seed from include/secrets.h and get
// written to NVS. On every boot after that, the NVS values win — even if a
// later flash (e.g. a CI-built self-update, see updater.cpp) was compiled
// with different placeholder secrets.h values. This is what keeps a
// self-updated device's real hostname/dashboard-password/OTA-password
// intact instead of reverting to whatever the CI build's secrets.h says.
namespace identity {

void begin();

const char* hostname();
const char* adminPassword();
const char* otaPassword();

// Clears the sticky NVS copies and reboots, so the next boot re-seeds from
// whatever's currently in secrets.h. Use this to deliberately rename a
// device (change OTA_HOSTNAME/passwords in secrets.h, flash, then call
// this once via the dashboard's /api/reset-identity).
void resetAndReboot();

}
