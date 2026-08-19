#pragma once
#include <cstdint>

namespace face {

void begin();
void update(uint32_t nowMs);

// Forces a random special animation to start immediately (e.g. from a
// button long-press), regardless of the idle random-trigger timer.
void triggerSpecial();

struct Snapshot {
    const char* eyes;
    const char* mouth;
};

// Current glyphs as of the last update() call, for mirroring elsewhere
// (e.g. the web admin dashboard's live view).
Snapshot getSnapshot();

// Skin (palette + idle expression) selection. Persisted to NVS, so it
// survives reboots. Index is clamped to [0, getSkinCount()-1].
void setSkin(int index);
int getSkin();
int getSkinCount();
const char* getSkinName(int index);

}
