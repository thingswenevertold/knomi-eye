#pragma once
#include <cstdint>

// Publishes a small public status snapshot (status/<hostname>.json) to the
// knomi-eye GitHub repo via the Contents API, so a remote "hub" device on a
// different network can read it via raw.githubusercontent.com without
// either device being directly reachable from the other. No-op if
// GITHUB_STATUS_TOKEN is empty in secrets.h.
namespace statuspublish {

void begin();
void tick(uint32_t nowMs); // call every loop tick; rate-limits its own pushes

}
