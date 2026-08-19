#include <Arduino.h>
#include "display/display.h"
#include "ui/face.h"
#include "ui/status.h"
#include "input/button.h"
#include "ota.h"
#include "web/admin_server.h"
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
    face::begin();
    status::begin();
    button::begin();

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
            face::triggerSpecial();
            break;
        case button::Event::None:
            break;
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

    ota::handle();
    if (adminActive) {
        admin::handle(now);
    }
    delay(16); // ~60 fps
}
