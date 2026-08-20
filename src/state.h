#pragma once
#include <cstdint>

// Persistent tamagotchi stats: energy decays over real elapsed time (needs
// timesync), regenerates on interaction (any button press). XP accrues from
// deliberate engagement (long-press specials) plus a small trickle for days
// alive. Everything persists to NVS and survives reboots/power loss.
namespace state {

void begin();

// Call every loop tick; internally rate-limits its own work.
void tick(uint32_t nowMs);

void onInteraction();      // any button press: small energy top-up
void onSpecialTriggered(); // user-triggered special animation: +XP

int energyPercent();  // 0-100
uint32_t xp();
uint32_t ageSeconds(); // 0 if birth timestamp not yet established (no time sync yet)

// Checks the skin unlock thresholds (see skins.h) against current xp().
// Returns the skin index that just crossed its threshold for the first
// time, or -1 if none. Call once per loop tick; each unlock fires once.
int pollNewUnlock();

}
