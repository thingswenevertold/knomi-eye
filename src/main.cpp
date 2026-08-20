#include <Arduino.h>
#include "display/display.h"
#include "ui/face.h"
#include "ui/tuning.h"
#include "ui/status.h"
#include "input/button.h"
#include "ota.h"
#include "web/admin_server.h"
#include "net/ble.h"
#include "net/wifiprov.h"
#include "diag.h"

namespace {
    enum class Screen { Face, Status };
    Screen screen = Screen::Face;
    int statusPage = 0;
    uint32_t statusUntilMs = 0;
    bool adminActive = false;
}

void setup() {
    display::begin();
    tuning::begin();   // owns the backlight; must precede the first palette read
    face::begin();
    status::begin();
    button::begin();

    // BLE first: it is the only control surface that works with no network
    // at all, so it must come up even if the WiFi association below fails —
    // and it is how a new network gets provisioned in the first place.
    wifiprov::begin();
    ble::begin();

    ota::begin();
    if (ota::isConnected()) {
        admin::begin();
        adminActive = true;
    }
}

void loop() {
    uint32_t now = millis();

    switch (button::poll(now)) {
        case button::Event::Click:
            diag::setButtonEvent("click");
            face::notifyInteraction(now);
            if (screen == Screen::Face) {
                screen = Screen::Status;
                statusPage = 0;
            } else {
                statusPage = (statusPage + 1) % status::pageCount();
            }
            statusUntilMs = now + 10000;
            break;
        case button::Event::LongPress:
            diag::setButtonEvent("long_press");
            face::notifyInteraction(now);
            face::triggerSpecial();
            break;
        case button::Event::None:
            break;
    }

    // WiFi can arrive long after boot, when a network is provisioned over
    // Bluetooth. Starting the web services only in setup() meant a device
    // that booted without a network never served its dashboard at all.
    if (!adminActive && ota::isConnected()) {
        ota::startServices();
        admin::begin();
        adminActive = true;
    }

    if (screen == Screen::Status && now >= statusUntilMs) {
        screen = Screen::Face;
    }

    diag::setScreen(screen == Screen::Face ? "face" : "status");

    if (screen == Screen::Face) {
        face::update(now);
    } else {
        status::update(now, statusPage);
    }

    ble::handle(now);
    ota::handle();
    if (adminActive) {
        admin::handle(now);
    }
    // Cadence sur la duree reelle de la frame, au lieu d'ajouter 16 ms a
    // celle-ci : le rendu coute deja plusieurs millisecondes, donc le "~60 fps"
    // annonce n'etait jamais atteint, et l'intervalle variait avec la charge.
    // On garde toujours au moins un delay(1) pour que WiFi et BLE aient leur
    // tour — les affamer coute bien plus cher que quelques images.
    const uint32_t FRAME_MS = 11;   // ~90 images par seconde, la dalle suit
    const uint32_t spent = millis() - now;
    delay(spent >= FRAME_MS ? 1 : FRAME_MS - spent);
}
