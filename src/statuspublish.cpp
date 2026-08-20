#include "statuspublish.h"

#if __has_include("../include/secrets.h")
#include "../include/secrets.h"
#else
#error "include/secrets.h missing: copy include/secrets.h.example to include/secrets.h"
#endif

#include "identity.h"
#include "state.h"
#include "weather.h"
#include "ui/face.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

static const char* REPO = "thingswenevertold/knomi-eye";

namespace {

uint32_t nextPushMs = 0;
constexpr uint32_t PUSH_INTERVAL_MS = 600000; // 10 min

const char* B64_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String base64Encode(const String& in) {
    String out;
    out.reserve((in.length() + 2) / 3 * 4);
    size_t i = 0;
    while (i + 2 < in.length()) {
        uint32_t n = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8) | (uint8_t)in[i + 2];
        out += B64_CHARS[(n >> 18) & 0x3F];
        out += B64_CHARS[(n >> 12) & 0x3F];
        out += B64_CHARS[(n >> 6) & 0x3F];
        out += B64_CHARS[n & 0x3F];
        i += 3;
    }
    size_t rem = in.length() - i;
    if (rem == 1) {
        uint32_t n = (uint8_t)in[i] << 16;
        out += B64_CHARS[(n >> 18) & 0x3F];
        out += B64_CHARS[(n >> 12) & 0x3F];
        out += "==";
    } else if (rem == 2) {
        uint32_t n = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8);
        out += B64_CHARS[(n >> 18) & 0x3F];
        out += B64_CHARS[(n >> 12) & 0x3F];
        out += B64_CHARS[(n >> 6) & 0x3F];
        out += "=";
    }
    return out;
}

String jsonEscape(const String& s) {
    String out;
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

String buildStatusJson() {
    face::Snapshot snap = face::getSnapshot();
    String json = "{";
    json += "\"device\":\"" + jsonEscape(identity::hostname()) + "\",";
    json += "\"firmware\":\"" + String(FIRMWARE_VERSION) + "\",";
    json += "\"skin\":\"" + jsonEscape(face::getSkinName(face::getSkin())) + "\",";
    json += "\"energy_pct\":" + String(state::energyPercent()) + ",";
    json += "\"xp\":" + String((unsigned long)state::xp()) + ",";
    json += "\"age_s\":" + String((unsigned long)state::ageSeconds()) + ",";
    json += "\"weather\":" + String((int)weather::current()) + ",";
    json += "\"eyes\":\"" + jsonEscape(snap.eyes) + "\",";
    json += "\"mouth\":\"" + jsonEscape(snap.mouth) + "\",";
    json += "\"updated_unix\":" + String((unsigned long)time(nullptr));
    json += "}";
    return json;
}

bool githubRequest(const String& method, const String& url, const String& body, String& responseOut, int& httpCode) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(8000);
    if (!http.begin(client, url)) return false;

    http.addHeader("Authorization", String("Bearer ") + GITHUB_STATUS_TOKEN);
    http.addHeader("Accept", "application/vnd.github+json");
    http.addHeader("User-Agent", "knomi-eye-status-publisher");
    if (body.length() > 0) {
        http.addHeader("Content-Type", "application/json");
    }

    httpCode = http.sendRequest(method.c_str(), (uint8_t*)body.c_str(), body.length());
    responseOut = http.getString();
    http.end();
    return true;
}

bool extractString(const String& body, const char* key, String& out) {
    int idx = body.indexOf(key);
    if (idx < 0) return false;
    idx += strlen(key);
    int start = body.indexOf('"', idx) + 1;
    int end = body.indexOf('"', start);
    if (start <= 0 || end < 0) return false;
    out = body.substring(start, end);
    return true;
}

void publish() {
    if (strlen(GITHUB_STATUS_TOKEN) == 0) return;
    if (WiFi.status() != WL_CONNECTED) return;

    String path = "status/" + String(identity::hostname()) + ".json";
    String apiUrl = "https://api.github.com/repos/" + String(REPO) + "/contents/" + path;

    // Look up the current file's sha (required by the Contents API to
    // update an existing file; omitted entirely on first-ever publish).
    String getResp;
    int getCode;
    String sha;
    if (githubRequest("GET", apiUrl + "?ref=master", "", getResp, getCode) && getCode == 200) {
        extractString(getResp, "\"sha\":", sha);
    }

    String content = base64Encode(buildStatusJson());
    String putBody = "{";
    putBody += "\"message\":\"status: " + String(identity::hostname()) + "\",";
    putBody += "\"content\":\"" + content + "\",";
    putBody += "\"branch\":\"master\"";
    if (sha.length() > 0) {
        putBody += ",\"sha\":\"" + sha + "\"";
    }
    putBody += "}";

    String putResp;
    int putCode;
    githubRequest("PUT", apiUrl, putBody, putResp, putCode);
}

}

namespace statuspublish {

void begin() {
    nextPushMs = 30000; // first push 30s after boot, let WiFi/time settle
}

void tick(uint32_t nowMs) {
    if (nowMs < nextPushMs) return;
    nextPushMs = nowMs + PUSH_INTERVAL_MS;
    publish();
}

}
