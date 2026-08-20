#include "ota.h"

#if __has_include("../include/secrets.h")
#include "../include/secrets.h"
#else
#error "include/secrets.h missing: copy include/secrets.h.example to include/secrets.h and fill in your WiFi credentials"
#endif

#include "display/display.h"
#include "identity.h"
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <WiFiManager.h>

namespace {

// Tries one network for up to timeoutMs. Returns true if connected.
bool tryConnect(const char* ssid, const char* password, uint32_t timeoutMs) {
    if (ssid == nullptr || ssid[0] == '\0') return false;

    WiFi.begin(ssid, password);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(200);
    }
    return WiFi.status() == WL_CONNECTED;
}

// Shown on the physical screen while the captive portal AP is open, so the
// device isn't just a blank/frozen face while someone is meant to be
// connecting to it with their phone.
void showSetupScreen(WiFiManager* mgr) {
    uint32_t bg = display::rgb(10, 10, 10);
    uint32_t accent = display::rgb(255, 90, 0);

    display::beginFrame();
    display::fillScreenNorm(bg);
    display::drawTextCenteredNorm(0.5f, 0.36f, 0.13f, "SETUP", accent);
    display::drawTextCenteredNorm(0.5f, 0.56f, 0.075f, mgr->getConfigPortalSSID().c_str(), accent);
    display::drawTextCenteredNorm(0.5f, 0.70f, 0.055f, "connect w/ phone", accent);
    display::endFrame(bg);
}

}

namespace ota {

void begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(identity::hostname());

    // Dev-only blocking wait: keeps setup() simple. If neither known network
    // is reachable, fall back to a WiFiManager captive portal so anyone
    // building this hardware (e.g. a friend) can configure their own WiFi
    // from a phone, without ever touching secrets.h or re-flashing.
    bool connected = tryConnect(WIFI_SSID_1, WIFI_PASSWORD_1, 6000);
    if (!connected) {
        connected = tryConnect(WIFI_SSID_2, WIFI_PASSWORD_2, 6000);
    }

    if (!connected) {
        WiFiManager wm;
        wm.setAPCallback(showSetupScreen);
        wm.setConfigPortalTimeout(180); // give up after 3 min so the device doesn't hang forever unattended
        connected = wm.autoConnect(identity::hostname());
    }

    if (connected) {
        ArduinoOTA.setHostname(identity::hostname());
        ArduinoOTA.setPassword(identity::otaPassword());
        ArduinoOTA.begin();
    }
}

void handle() {
    if (WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.handle();
    }
}

bool isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

}
