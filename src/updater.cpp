#include "updater.h"
#include "display/display.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

// Published by .github/workflows/build-firmware.yml on every push to master.
static const char* VERSION_URL  = "https://raw.githubusercontent.com/thingswenevertold/knomi-eye/master/firmware/version.txt";
static const char* FIRMWARE_URL = "https://raw.githubusercontent.com/thingswenevertold/knomi-eye/master/firmware/firmware.bin";

namespace {

enum class State { Idle, Checking, Available, UpToDate, Applying, Error };
State state = State::Idle;

char remoteVersion[32] = "";
char errorMsg[32] = "";
uint32_t resultUntilMs = 0;
uint32_t confirmUntilMs = 0;
int applyProgress = 0;

void drawMessage(const char* line1, const char* line2) {
    uint32_t bg = display::rgb(10, 10, 10);
    uint32_t ac = display::rgb(255, 90, 0);
    display::beginFrame();
    display::fillScreenNorm(bg);
    display::drawTextCenteredNorm(0.5f, 0.40f, 0.10f, line1, ac);
    if (line2 && line2[0]) {
        display::drawTextCenteredNorm(0.5f, 0.58f, 0.06f, line2, ac);
    }
    display::endFrame(bg);
}

void drawProgress(int pct) {
    uint32_t bg = display::rgb(10, 10, 10);
    uint32_t ac = display::rgb(255, 90, 0);
    display::beginFrame();
    display::fillScreenNorm(bg);
    display::drawTextCenteredNorm(0.5f, 0.34f, 0.09f, "UPDATING", ac);
    display::fillRectNorm(0.15f, 0.5f, 0.70f, 0.08f, display::rgb(40, 30, 20));
    display::fillRectNorm(0.15f, 0.5f, 0.70f * (pct / 100.0f), 0.08f, ac);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    display::drawTextCenteredNorm(0.5f, 0.66f, 0.07f, buf, ac);
    display::endFrame(bg);
}

bool fetchText(const char* url, String& out) {
    WiFiClientSecure client;
    client.setInsecure(); // no cert pinning — acceptable trade-off for a hobby OTA channel
    HTTPClient http;
    http.setTimeout(5000);
    if (!http.begin(client, url)) return false;
    int code = http.GET();
    bool ok = (code == 200);
    if (ok) out = http.getString();
    http.end();
    return ok;
}

}

namespace updater {

void begin() {}

void startCheck() {
    state = State::Checking;
    drawMessage("CHECKING", "for updates...");

    String remote;
    if (!fetchText(VERSION_URL, remote)) {
        state = State::Error;
        snprintf(errorMsg, sizeof(errorMsg), "network error");
        resultUntilMs = millis() + 2000;
        return;
    }
    remote.trim();
    snprintf(remoteVersion, sizeof(remoteVersion), "%s", remote.c_str());

    if (remote == FIRMWARE_VERSION) {
        state = State::UpToDate;
        resultUntilMs = millis() + 1800;
    } else {
        state = State::Available;
        confirmUntilMs = millis() + 6000;
    }
}

bool isActive() {
    return state != State::Idle;
}

bool isAwaitingConfirm() {
    return state == State::Available;
}

void confirm() {
    if (state != State::Available) return;
    state = State::Applying;
    applyProgress = 0;
    drawProgress(0);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(15000);

    if (!http.begin(client, FIRMWARE_URL)) {
        state = State::Error;
        snprintf(errorMsg, sizeof(errorMsg), "download failed");
        resultUntilMs = millis() + 2000;
        return;
    }

    int code = http.GET();
    int len = http.getSize();
    if (code != 200 || len <= 0) {
        http.end();
        state = State::Error;
        snprintf(errorMsg, sizeof(errorMsg), "download failed");
        resultUntilMs = millis() + 2000;
        return;
    }

    if (!Update.begin(len)) {
        http.end();
        state = State::Error;
        snprintf(errorMsg, sizeof(errorMsg), "no space");
        resultUntilMs = millis() + 2000;
        return;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[512];
    int written = 0;
    while (http.connected() && written < len) {
        size_t avail = stream->available();
        if (avail) {
            int n = stream->readBytes(buf, avail < sizeof(buf) ? avail : sizeof(buf));
            Update.write(buf, n);
            written += n;
            int pct = (int)(100.0f * written / len);
            if (pct != applyProgress) {
                applyProgress = pct;
                drawProgress(pct);
            }
        } else {
            delay(2);
        }
    }
    http.end();

    if (written != len || !Update.end(true)) {
        state = State::Error;
        snprintf(errorMsg, sizeof(errorMsg), "flash failed");
        resultUntilMs = millis() + 2000;
        return;
    }

    drawMessage("REBOOTING", "");
    delay(500);
    ESP.restart();
}

void update(uint32_t nowMs) {
    switch (state) {
        case State::Available:
            drawMessage("UPDATE AVAILABLE", "click BOOT to confirm");
            if (nowMs >= confirmUntilMs) {
                state = State::Idle;
            }
            break;
        case State::UpToDate:
            drawMessage("UP TO DATE", FIRMWARE_VERSION);
            if (nowMs >= resultUntilMs) state = State::Idle;
            break;
        case State::Error:
            drawMessage("UPDATE FAILED", errorMsg);
            if (nowMs >= resultUntilMs) state = State::Idle;
            break;
        default:
            break;
    }
}

}
