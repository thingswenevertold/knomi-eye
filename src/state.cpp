#include "state.h"
#include "timesync.h"
#include "ui/skins.h"
#include <Arduino.h>
#include <Preferences.h>

namespace {
    Preferences prefs;

    uint32_t bornEpoch = 0;      // 0 = not yet established
    uint32_t lastDecayEpoch = 0; // last real-time instant we applied decay
    float energy = 80.0f;
    uint32_t xpTotal = 0;
    uint32_t visits = 0;

    uint32_t lastTickMs = 0;
    constexpr uint32_t TICK_INTERVAL_MS = 15000; // rate-limit NVS writes

    int highestUnlockedIndex = 0; // persisted; skins up to and including this index have been celebrated

    constexpr float DECAY_PER_SECOND = 100.0f / (12.0f * 3600.0f); // empty in ~12h
    constexpr float INTERACTION_GAIN = 5.0f;

    void save() {
        prefs.putUInt("born", bornEpoch);
        prefs.putFloat("energy", energy);
        prefs.putUInt("xp", xpTotal);
        prefs.putUInt("visits", visits);
    }
}

namespace state {

void begin() {
    prefs.begin("tamagotchi", false);
    bornEpoch = prefs.getUInt("born", 0);
    energy = prefs.getFloat("energy", 80.0f);
    xpTotal = prefs.getUInt("xp", 0);
    visits = prefs.getUInt("visits", 0);
    highestUnlockedIndex = prefs.getInt("unlocked", 0);
    lastDecayEpoch = bornEpoch;
}

void tick(uint32_t nowMs) {
    if (nowMs - lastTickMs < TICK_INTERVAL_MS) return;
    lastTickMs = nowMs;

    if (!timesync::isSynced()) return;
    uint32_t now = timesync::epoch();
    if (now < 1700000000) return; // sanity: reject bogus pre-2023 epoch

    if (bornEpoch == 0) {
        bornEpoch = now;
        lastDecayEpoch = now;
        save();
        return;
    }

    if (lastDecayEpoch == 0) lastDecayEpoch = now;
    uint32_t elapsed = (now > lastDecayEpoch) ? (now - lastDecayEpoch) : 0;
    if (elapsed > 0) {
        energy -= DECAY_PER_SECOND * elapsed;
        if (energy < 0.0f) energy = 0.0f;
        if (energy > 100.0f) energy = 100.0f;
        lastDecayEpoch = now;
        save();
    }
}

void onInteraction() {
    energy += INTERACTION_GAIN;
    if (energy > 100.0f) energy = 100.0f;
    save();
}

void onSpecialTriggered() {
    xpTotal += 2;
    save();
}

void onTagScan(int xpBonus, int energyBonus) {
    xpTotal += xpBonus;
    energy += energyBonus;
    if (energy > 100.0f) energy = 100.0f;
    if (energy < 0.0f) energy = 0.0f;
    visits++;
    save();
}

int energyPercent() {
    return (int)energy;
}

uint32_t xp() {
    uint32_t ageDays = ageSeconds() / 86400;
    return xpTotal + ageDays;
}

uint32_t ageSeconds() {
    if (bornEpoch == 0 || !timesync::isSynced()) return 0;
    uint32_t now = timesync::epoch();
    return (now > bornEpoch) ? (now - bornEpoch) : 0;
}

uint32_t visitCount() {
    return visits;
}

int pollNewUnlock() {
    int next = highestUnlockedIndex + 1;
    if (next >= SKIN_COUNT) return -1;
    if (xp() < SKINS[next].unlockXp) return -1;

    highestUnlockedIndex = next;
    prefs.putInt("unlocked", highestUnlockedIndex);
    return next;
}

}
