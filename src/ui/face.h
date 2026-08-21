#pragma once
#include <cstdint>

namespace face {

void begin();
void update(uint32_t nowMs);

// Forces a random special animation to start immediately (e.g. from a
// button long-press), regardless of the idle random-trigger timer.
void triggerSpecial();

// Plays one *named* animation immediately, for a remote control that wants a
// specific move rather than a surprise. Returns false and changes nothing if
// the name is not one of getAnimName()'s.
bool playAnim(const char* name);

// The animations playAnim() accepts. A client should read these rather than
// hardcode them, so adding an animation to the firmware is enough to make it
// appear in every remote.
int getAnimCount();
const char* getAnimName(int index);

// Someone physically interacted with the device. Wakes the creature, makes
// it attentive for a while, and provokes a pleased reaction. Call this for
// any button event — see mood.h for what it drives.
void notifyInteraction(uint32_t nowMs);

// Current mood as a short lowercase word ("idle", "bored", "asleep", ...),
// for the web dashboard.
const char* moodName();

// Re-derives the drawing colours from the active skin and whatever the
// tuning module currently holds. Call after changing either.
void refreshPalette();

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

// Creature retiree : son creneau existe toujours, pour ne pas decaler les
// index persistes en NVS, mais elle ne doit apparaitre dans aucune liste.
bool isSkinHidden(int index);

}
