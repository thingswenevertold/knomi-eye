#pragma once
#include <cstdint>

// Weather via two free, keyless public APIs: ip-api.com resolves an
// approximate lat/lon from the device's public IP once, then Open-Meteo
// gives current conditions at that location, refreshed periodically.
namespace weather {

enum class Condition { Unknown, Clear, Cloudy, Rain, Snow, Storm, Fog };

void begin();
void tick(uint32_t nowMs); // call every loop tick; rate-limits its own fetches

Condition current();
const char* debugInfo(); // last error / step reached, for troubleshooting

}
