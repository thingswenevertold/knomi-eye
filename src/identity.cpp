#include "identity.h"

#if __has_include("../include/secrets.h")
#include "../include/secrets.h"
#else
#error "include/secrets.h missing: copy include/secrets.h.example to include/secrets.h"
#endif

#include <Arduino.h>
#include <Preferences.h>

namespace {
    Preferences prefs;
    char hostnameBuf[32];
    char adminPwBuf[32];
    char otaPwBuf[32];

    void loadOrSeed(const char* key, const char* fallback, char* out, size_t outSize) {
        String v = prefs.getString(key, "");
        if (v.length() == 0) {
            v = fallback;
            prefs.putString(key, v);
        }
        snprintf(out, outSize, "%s", v.c_str());
    }
}

namespace identity {

void begin() {
    prefs.begin("identity", false);
    loadOrSeed("hostname", OTA_HOSTNAME, hostnameBuf, sizeof(hostnameBuf));
    loadOrSeed("admin_pw", ADMIN_PASSWORD, adminPwBuf, sizeof(adminPwBuf));
    loadOrSeed("ota_pw", OTA_PASSWORD, otaPwBuf, sizeof(otaPwBuf));
}

const char* hostname() { return hostnameBuf; }
const char* adminPassword() { return adminPwBuf; }
const char* otaPassword() { return otaPwBuf; }

}
