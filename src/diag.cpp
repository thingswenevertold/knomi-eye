#include "diag.h"

namespace {
    const char* lastButtonEvent = "none";
    const char* currentScreen = "face";
    float fps = 0.0f;
    uint32_t drawUs = 0, netUs = 0, totalUs = 0;
    uint32_t bleUs = 0, otaUs = 0, adminUs = 0;
    uint32_t cleanUs = 0, jsonUs = 0, sendUs = 0;
}

namespace diag {

void setButtonEvent(const char* name) { lastButtonEvent = name; }
const char* getButtonEvent() { return lastButtonEvent; }

void setScreen(const char* name) { currentScreen = name; }
const char* getScreen() { return currentScreen; }

void setFps(float value) { fps = value; }
float getFps() { return fps; }

void setTimings(uint32_t d, uint32_t n, uint32_t t) { drawUs = d; netUs = n; totalUs = t; }
uint32_t getDrawUs()  { return drawUs; }

static uint32_t pushUs = 0;
void setPushUs(uint32_t us) { pushUs = us; }
uint32_t getPushUs() { return pushUs; }
uint32_t getNetUs()   { return netUs; }
uint32_t getTotalUs() { return totalUs; }

void setNetSplit(uint32_t b, uint32_t o, uint32_t a) { bleUs = b; otaUs = o; adminUs = a; }
uint32_t getBleUs()   { return bleUs; }
uint32_t getOtaUs()   { return otaUs; }
uint32_t getAdminUs() { return adminUs; }

void setAdminSplit(uint32_t c, uint32_t j, uint32_t s) { cleanUs = c; jsonUs = j; sendUs = s; }
uint32_t getCleanUs() { return cleanUs; }
uint32_t getJsonUs()  { return jsonUs; }
uint32_t getSendUs()  { return sendUs; }

}
