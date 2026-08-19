#pragma once
#include <cstdint>

// Simple text status screens, shown while cycling through BOOT-button clicks.
namespace status {

void begin();
int pageCount();
void update(uint32_t nowMs, int page);

}
