#include "mood.h"
#include <Arduino.h>
#include <WiFi.h>

namespace {

// How long an interaction keeps the creature attentive.
constexpr uint32_t ENGAGED_MS = 20000;
// Neglect thresholds.
constexpr uint32_t BORED_AFTER_MS  = 3UL * 60UL * 1000UL;
constexpr uint32_t ASLEEP_AFTER_MS = 12UL * 60UL * 1000UL;
// Below this the radio is considered poor enough to fidget about.
constexpr int WEAK_RSSI_DBM = -80;

mood::State state = mood::State::Idle;
uint32_t lastInteractionMs = 0;

// Radio is sampled on a slow cadence: WiFi.RSSI() is not free, and a mood
// that flickers with every noisy sample would read as a bug, not a feeling.
constexpr uint32_t RADIO_POLL_MS = 3000;
uint32_t nextRadioPollMs = 0;
bool radioConnected = true;
int radioRssi = -50;

struct Tuning {
    uint32_t blinkLo, blinkHi;
    uint32_t specialLo, specialHi;
    float energy;
    const char* name;
};

// One row per state. This table *is* the personality — everything else
// just reads from it.
const Tuning TUNING[] = {
    /* Engaged */ {  1500,  3500,   3000,   7000, 1.00f, "engaged" },
    /* Idle    */ {  2500,  6000,  20000,  45000, 0.70f, "idle"    },
    /* Uneasy  */ {  1200,  3000,  12000,  25000, 0.80f, "uneasy"  },
    /* Bored   */ {  4000,  9000,  45000,  90000, 0.40f, "bored"   },
    /* Asleep  */ { 20000, 40000, 120000, 240000, 0.12f, "asleep"  },
    /* Lost    */ {  2000,  5000,  15000,  30000, 0.60f, "lost"    },
};

const Tuning& tuning() {
    return TUNING[static_cast<uint8_t>(state)];
}

}

namespace mood {

void begin() {
    lastInteractionMs = millis();
    state = State::Engaged;   // a fresh boot counts as being paid attention to
    nextRadioPollMs = 0;
}

void notifyInteraction(uint32_t now) {
    lastInteractionMs = now;
    state = State::Engaged;
}

uint32_t idleForMs(uint32_t now) {
    return now - lastInteractionMs;   // unsigned arithmetic survives rollover
}

void update(uint32_t now) {
    if (now >= nextRadioPollMs) {
        nextRadioPollMs = now + RADIO_POLL_MS;
        radioConnected = (WiFi.status() == WL_CONNECTED);
        radioRssi = radioConnected ? WiFi.RSSI() : -127;
    }

    const uint32_t idle = idleForMs(now);

    // Ordered by precedence: a recent touch outranks everything, and a
    // missing network outranks a merely weak one.
    if (idle < ENGAGED_MS)              state = State::Engaged;
    else if (!radioConnected)           state = State::Lost;
    else if (idle >= ASLEEP_AFTER_MS)   state = State::Asleep;
    else if (radioRssi < WEAK_RSSI_DBM) state = State::Uneasy;
    else if (idle >= BORED_AFTER_MS)    state = State::Bored;
    else                                state = State::Idle;
}

State get() { return state; }
const char* name() { return tuning().name; }

void blinkInterval(uint32_t& lo, uint32_t& hi) {
    lo = tuning().blinkLo;
    hi = tuning().blinkHi;
}

void specialInterval(uint32_t& lo, uint32_t& hi) {
    lo = tuning().specialLo;
    hi = tuning().specialHi;
}

float energy() { return tuning().energy; }

}
