#include "status.h"
#include "../display/display.h"
#include <WiFi.h>
#include <Arduino.h>

namespace {
    uint32_t colorBg;
    uint32_t colorAccent;
    uint32_t colorDim;
}

namespace status {

void begin() {
    colorBg     = display::rgb(12, 11, 10);
    colorAccent = display::rgb(255, 90, 0);
    colorDim    = display::rgb(140, 80, 30);
}

int pageCount() { return 3; }

void update(uint32_t /*now*/, int page) {
    char label[16];
    char value[40];

    switch (page) {
        case 0:
            snprintf(label, sizeof(label), "WIFI");
            snprintf(value, sizeof(value), "%s",
                     WiFi.isConnected() ? WiFi.SSID().c_str() : "disconnected");
            break;
        case 1:
            snprintf(label, sizeof(label), "IP");
            snprintf(value, sizeof(value), "%s",
                     WiFi.isConnected() ? WiFi.localIP().toString().c_str() : "-");
            break;
        default:
            snprintf(label, sizeof(label), "SIGNAL");
            if (WiFi.isConnected()) {
                snprintf(value, sizeof(value), "%d dBm", WiFi.RSSI());
            } else {
                snprintf(value, sizeof(value), "-");
            }
            break;
    }

    display::beginFrame();
    display::fillScreenNorm(colorBg);
    display::drawCircleNorm(0.5f, 0.5f, 0.47f, 0.006f, colorDim);
    display::drawTextCenteredNorm(0.5f, 0.40f, 0.10f, label, colorDim);
    display::drawTextCenteredNorm(0.5f, 0.56f, 0.09f, value, colorAccent);
    display::endFrame(colorBg);
}

}
