#include "diag.h"

namespace {
    const char* lastButtonEvent = "none";
    const char* currentScreen = "face";
}

namespace diag {

void setButtonEvent(const char* name) { lastButtonEvent = name; }
const char* getButtonEvent() { return lastButtonEvent; }

void setScreen(const char* name) { currentScreen = name; }
const char* getScreen() { return currentScreen; }

}
