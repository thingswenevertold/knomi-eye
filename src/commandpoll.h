#pragma once
#include <cstdint>

// Reads commands/<hostname>.json from the knomi-eye GitHub repo (public,
// no auth needed) — the reverse direction of statuspublish.cpp. This is
// how a remote hub (different network, e.g. at work) can tell this device
// "someone scanned your RFID tag" without either side being reachable from
// the other. Each command carries a sequence number; only ones newer than
// the last one we've already applied (persisted in NVS) take effect, so a
// command is never double-applied across reboots or repeated polls.
namespace commandpoll {

void begin();
void tick(uint32_t nowMs); // call every loop tick; rate-limits its own fetches

}
