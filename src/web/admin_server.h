#pragma once
#include <cstdint>

// Retro-styled web dashboard, reachable at http://<OTA_HOSTNAME>.local/
// once connected to WiFi. Shows device status, LittleFS content, live
// resource-usage bars, and a WebSocket mirror of the current face.
// Dev/debug tool, not part of the on-device tamagotchi experience.
namespace admin {

void begin();

// Call every loop tick once connected; throttles its own broadcast rate
// internally, so this is cheap to call unconditionally.
void handle(uint32_t nowMs);

}
