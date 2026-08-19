#pragma once

// Tiny cross-module debug state, surfaced in the web admin's /api/status —
// useful when physical serial capture is unreliable (this board's case).
namespace diag {

void setButtonEvent(const char* name);
const char* getButtonEvent();

void setScreen(const char* name);
const char* getScreen();

}
