#pragma once
#include <cstdint>

// Self-update over HTTPS: fetches firmware.bin published by CI (see
// .github/workflows/build-firmware.yml) from the repo's `firmware/` folder
// and flashes itself via the ESP32 Update API. Triggered by a 5s BOOT hold.
namespace updater {

void begin();

// Call on a 5s+ BOOT hold (button::Event::VeryLongPress). Blocks briefly
// while checking for a new version.
void startCheck();

// True whenever the updater owns the screen (checking/available/applying/
// result message) — main.cpp should skip normal face/status rendering.
bool isActive();

// True while showing "update available, click to confirm".
bool isAwaitingConfirm();

// Call on a Click while isAwaitingConfirm() is true. Blocks for the whole
// download+flash+reboot — there's no coming back from this call on success.
void confirm();

// Call every loop tick while isActive(); draws the current state and
// handles the confirm-window timeout.
void update(uint32_t nowMs);

}
