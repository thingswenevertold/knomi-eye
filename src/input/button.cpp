#include "button.h"
#include <Arduino.h>

namespace {
    constexpr int PIN = 0; // BOOT, active low, has its own pull-up
    constexpr uint32_t DEBOUNCE_MS = 30;
    constexpr uint32_t LONG_PRESS_MS = 1200;
    constexpr uint32_t VERY_LONG_PRESS_MS = 5000;

    bool stableLevel = true; // HIGH = released
    bool rawLevel = true;
    uint32_t lastEdgeMs = 0;
    uint32_t pressStartMs = 0;
    bool longFired = false;
    bool veryLongFired = false;
}

namespace button {

void begin() {
    pinMode(PIN, INPUT_PULLUP);
    stableLevel = rawLevel = digitalRead(PIN);
}

Event poll(uint32_t now) {
    bool raw = digitalRead(PIN);
    if (raw != rawLevel) {
        rawLevel = raw;
        lastEdgeMs = now;
    }

    Event ev = Event::None;

    if (raw != stableLevel && now - lastEdgeMs > DEBOUNCE_MS) {
        stableLevel = raw;
        if (stableLevel == LOW) {
            pressStartMs = now;
            longFired = false;
            veryLongFired = false;
        } else if (!longFired) {
            ev = Event::Click;
        }
    }

    if (stableLevel == LOW && !veryLongFired && now - pressStartMs >= VERY_LONG_PRESS_MS) {
        veryLongFired = true;
        longFired = true; // suppress the plain LongPress fun-trigger for this hold
        ev = Event::VeryLongPress;
    } else if (stableLevel == LOW && !longFired && now - pressStartMs >= LONG_PRESS_MS) {
        longFired = true;
        ev = Event::LongPress;
    }

    return ev;
}

bool isHeld() {
    return stableLevel == LOW;
}

uint32_t heldForMs(uint32_t now) {
    return (stableLevel == LOW) ? (now - pressStartMs) : 0;
}

}
