#include <Arduino.h>
#include "display/display.h"
#include "ui/face.h"
#include "ui/status.h"
#include "input/button.h"
#include "ota.h"
#include "web/admin_server.h"
#include "diag.h"
#include "timesync.h"
#include "state.h"
#include "weather.h"
#include "updater.h"
#include "identity.h"
#include "statuspublish.h"

namespace {
    enum class Screen { Face, Status };
    Screen screen = Screen::Face;
    int statusPage = 0;
    uint32_t statusUntilMs = 0;
    bool adminActive = false;
}

void setup() {
    identity::begin();
    display::begin();
    face::begin();
    status::begin();
    button::begin();

    state::begin();
    weather::begin();
    updater::begin();
    statuspublish::begin();

    ota::begin();
    if (ota::isConnected()) {
        admin::begin();
        adminActive = true;
        timesync::begin();
    }
}

void loop() {
    uint32_t now = millis();

    switch (button::poll(now)) {
        case button::Event::Click:
            diag::setButtonEvent("click");
            if (updater::isAwaitingConfirm()) {
                updater::confirm(); // blocks: downloads, flashes, reboots (or falls through to Error on failure)
                break;
            }
            state::onInteraction();
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
            state::onInteraction();
            state::onSpecialTriggered();
            face::triggerSpecial();
            break;
        case button::Event::VeryLongPress:
            diag::setButtonEvent("very_long_press");
            updater::startCheck();
            break;
        case button::Event::None:
            break;
    }

    if (screen == Screen::Status && now >= statusUntilMs) {
        screen = Screen::Face;
    }

    diag::setScreen(screen == Screen::Face ? "face" : "status");

    if (updater::isActive()) {
        updater::update(now);
    } else if (screen == Screen::Face) {
        face::update(now);
    } else {
        status::update(now, statusPage);
    }

    ota::handle();
    if (adminActive) {
        admin::handle(now);
    }
    state::tick(now);
    weather::tick(now);
    statuspublish::tick(now);

    int unlocked = state::pollNewUnlock();
    if (unlocked >= 0) {
        face::celebrateUnlock(face::getSkinName(unlocked));
    }

    delay(16); // ~60 fps
}
