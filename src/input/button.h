#pragma once
#include <cstdint>

// BOOT button (GPIO0) is the only physical input on KNOMI V1.
namespace button {

enum class Event { None, Click, LongPress, VeryLongPress };

void begin();
Event poll(uint32_t nowMs);

// For UX flows that need continuous hold state (e.g. the update-confirm
// popup), not just discrete events.
bool isHeld();
uint32_t heldForMs(uint32_t nowMs); // 0 if not currently held

}
