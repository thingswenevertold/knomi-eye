#pragma once
#include <cstdint>

// Reactive personality layer.
//
// The face used to be driven purely by timers: a special animation fired
// every 4-9 seconds whether or not anything had happened. This module
// replaces that with a state derived from things that actually occur —
// how long since someone last touched the button, whether WiFi is healthy,
// whether the creature has been left alone all afternoon.
//
// face.cpp asks this module how often to blink and how often to act, so
// tuning the personality means editing the table in mood.cpp, not hunting
// constants scattered through the draw code.
namespace mood {

enum class State : uint8_t {
    Engaged,  // touched in the last few seconds — lively, attentive
    Idle,     // awake and calm, the normal resting state
    Uneasy,   // WiFi signal is poor; fidgety, blinks more
    Bored,    // minutes without interaction; slow, droopy
    Asleep,   // left alone a long time; eyes shut, rare stirring
    Lost,     // WiFi is gone; searching, unsettled
};

void begin();

// Call once per frame. Recomputes the state from elapsed time and radio health.
void update(uint32_t nowMs);

// Someone pressed the button. Wakes the creature and makes it attentive.
void notifyInteraction(uint32_t nowMs);

State get();
const char* name();

// Milliseconds since the last physical interaction.
uint32_t idleForMs(uint32_t nowMs);

// Scheduling hints for face.cpp. Ranges, in milliseconds.
void blinkInterval(uint32_t& lo, uint32_t& hi);
void specialInterval(uint32_t& lo, uint32_t& hi);

// 0.0 (fast asleep) .. 1.0 (fully engaged). Scales animation amplitude and
// ASCII-art frame rate, so a bored creature moves visibly more slowly.
float energy();

}
