#pragma once
#include <Arduino.h>
#include <cstdint>

// Live-tunable settings, shared by every control surface.
//
// The web dashboard and the BLE service both talk to this one module, and
// both speak the same little JSON document, so a value only ever has one
// definition and one persistence path. Everything here survives a reboot in
// NVS.
//
// What is NOT tunable at runtime: the shape of the ASCII art. Those frames
// are generated on a PC by assets/gen_cat_ascii.py and compiled in, so
// changing the cat's geometry means regenerating and reflashing. Colour,
// speed, brightness and skin choice are all instant.
namespace tuning {

struct State {
    // When false the active skin's own palette is used and the colour
    // fields below are merely a copy of it, kept current so a UI has
    // something sensible to display.
    bool colorOverride;
    uint8_t bgR, bgG, bgB;
    uint8_t fgR, fgG, fgB;
    uint8_t accR, accG, accB;

    uint8_t brightness;   // 0..255, panel backlight
    uint16_t speedPct;    // 25..400, animation speed as a percentage
};

void begin();

const State& get();

// Mirrors the active skin's palette into the state without enabling the
// override. Called when a skin is selected, so the UI's colour pickers show
// what is actually on screen.
void adoptSkinColors(uint8_t bgR, uint8_t bgG, uint8_t bgB,
                     uint8_t fgR, uint8_t fgG, uint8_t fgB,
                     uint8_t accR, uint8_t accG, uint8_t accB);

void setColors(uint8_t bgR, uint8_t bgG, uint8_t bgB,
               uint8_t fgR, uint8_t fgG, uint8_t fgB,
               uint8_t accR, uint8_t accG, uint8_t accB);
void clearColorOverride();

void setBrightness(uint8_t level);
void setSpeedPct(uint16_t pct);

// Animation speed as a plain multiplier, for the render code.
float speedScale();

// The wire format, identical over HTTP and BLE.
String toJson();

// Accepts any subset of the fields in toJson(). Unknown keys are ignored,
// so an older client talking to newer firmware degrades quietly instead of
// failing. Returns true if anything changed.
bool applyJson(const String& body);

}
