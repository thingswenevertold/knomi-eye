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

}
