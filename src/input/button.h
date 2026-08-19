#pragma once
#include <cstdint>

// BOOT button (GPIO0) is the only physical input on KNOMI V1.
namespace button {

enum class Event { None, Click, LongPress };

void begin();
Event poll(uint32_t nowMs);

}
