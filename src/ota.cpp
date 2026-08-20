#include "ota.h"
#include "net/wifiprov.h"

#if __has_include("../include/secrets.h")
#include "../include/secrets.h"
#else
#error "include/secrets.h missing: copy include/secrets.h.example to include/secrets.h and fill in your WiFi credentials"
#endif

#include <WiFi.h>
#include <ArduinoOTA.h>

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

}

namespace ota {

void startServices() {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.begin();
}

void begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(OTA_HOSTNAME);

    // The network provisioned over Bluetooth wins: it is the one chosen most
    // recently and on purpose. The networks compiled into secrets.h are only
    // a fallback for a device that has never been provisioned.
    bool connected = wifiprov::joinBest(8000);
    if (!connected) connected = tryConnect(WIFI_SSID_1, WIFI_PASSWORD_1, 6000);
    if (!connected) connected = tryConnect(WIFI_SSID_2, WIFI_PASSWORD_2, 6000);

    if (connected) startServices();
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
