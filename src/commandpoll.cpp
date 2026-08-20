#include "commandpoll.h"
#include "identity.h"
#include "state.h"
#include "ui/face.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>

static const char* REPO = "thingswenevertold/knomi-eye";

namespace {
    Preferences prefs;
    uint32_t lastSeq = 0;
    uint32_t nextPollMs = 0;
    constexpr uint32_t POLL_INTERVAL_MS = 45000; // ~45s: responsive without hammering GitHub

    bool extractLong(const String& body, const char* key, long& out) {
        int idx = body.indexOf(key);
        if (idx < 0) return false;
        idx += strlen(key);
        while (idx < (int)body.length() && body[idx] == ' ') idx++;
        int start = idx;
        while (idx < (int)body.length() && isDigit(body[idx])) idx++;
        if (idx == start) return false;
        out = body.substring(start, idx).toInt();
        return true;
    }

    void poll() {
        if (WiFi.status() != WL_CONNECTED) return;

        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        http.setTimeout(6000);

        String url = "https://raw.githubusercontent.com/" + String(REPO) +
                     "/master/commands/" + String(identity::hostname()) + ".json";
        if (!http.begin(client, url)) return;

        int code = http.GET();
        if (code != 200) {
            http.end();
            return; // 404 = no command ever sent yet, nothing to do
        }
        String body = http.getString();
        http.end();

        long seq = 0, xpBonus = 0, energyBonus = 0;
        if (!extractLong(body, "\"seq\":", seq)) return;
        if ((uint32_t)seq <= lastSeq) return; // already applied this one

        extractLong(body, "\"xp\":", xpBonus);
        extractLong(body, "\"energy\":", energyBonus);

        state::onTagScan((int)xpBonus, (int)energyBonus);
        face::celebrateVisit((int)xpBonus, (int)energyBonus);

        lastSeq = (uint32_t)seq;
        prefs.putUInt("last_seq", lastSeq);
    }
}

namespace commandpoll {

void begin() {
    prefs.begin("cmdpoll", false);
    lastSeq = prefs.getUInt("last_seq", 0);
    nextPollMs = 20000; // first check 20s after boot
}

void tick(uint32_t nowMs) {
    if (nowMs < nextPollMs) return;
    nextPollMs = nowMs + POLL_INTERVAL_MS;
    poll();
}

}
