#include "face.h"
#include "../display/display.h"
#include "skins.h"
#include "../assets/cat.h"
#include "../assets/google.h"
#include "../timesync.h"
#include "../weather.h"
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <cstdlib>
#include <cmath>

// Multi-skin ASCII face. Most skins share one generic template (ring +
// centered eyes/mouth glyphs); a couple of skins (Coraline, Glitch) use a
// completely different composition, dispatched by layout in update().

namespace {

enum class Special { None, Wink, Dance, Wobble, Surprised };

uint32_t colorBg;
uint32_t colorFace;
uint32_t colorRing;
uint32_t colorGlitchCyan;
uint32_t colorGlitchMagenta;
bool ringEnabled = true;
uint8_t layout = LAYOUT_DEFAULT;
const char* idleEyes = "o o";
const char* idleMouth = "-";
int skinIndex = 0;

Preferences prefs;

bool blinking = false;
uint32_t blinkUntilMs = 0;
uint32_t nextBlinkMs = 0;

Special special = Special::None;
uint32_t specialUntilMs = 0;
uint32_t nextSpecialMs = 0;

const char* lastEyesText = "o o";
const char* lastMouthText = "-";

char bannerText[24] = "";
uint32_t bannerUntilMs = 0;

uint32_t seenMask = 0; // bit i set once skin i has been applied at least once

enum class GlitchPayload { Ascii, Static, Cat, Google };

bool glitchBurst = false;
uint32_t glitchUntilMs = 0;
uint32_t nextGlitchCheckMs = 0;
GlitchPayload glitchPayload = GlitchPayload::Ascii;

uint32_t randRange(uint32_t lo, uint32_t hi) {
    return lo + (rand() % (hi - lo + 1));
}

bool isNight() {
    int hr = timesync::hour();
    return hr >= 0 && (hr < 6 || hr >= 23);
}

// Smoothly ramps brightness down 22:30-23:30 and back up 05:30-06:30,
// instead of an abrupt jump at the night-mode boundary.
uint8_t computeBrightness() {
    float hf = timesync::hourFraction();
    if (hf < 0.0f) return 255;

    constexpr uint8_t DAY = 255, NIGHT = 60;
    if (hf >= 6.5f && hf < 22.5f) return DAY;
    if (hf < 5.5f || hf >= 23.5f) return NIGHT;

    float t;
    if (hf < 6.5f) {
        t = (hf - 5.5f); // ramp up, 05:30-06:30
        return (uint8_t)(NIGHT + (DAY - NIGHT) * t);
    }
    t = (hf - 22.5f); // ramp down, 22:30-23:30
    return (uint8_t)(DAY - (DAY - NIGHT) * t);
}

void scheduleNextBlink(uint32_t now) {
    nextBlinkMs = now + (isNight() ? randRange(6000, 12000) : randRange(2000, 5000));
}

void scheduleNextSpecial(uint32_t now) {
    nextSpecialMs = now + (isNight() ? randRange(20000, 40000) : randRange(4000, 9000));
}

Special pickSpecial() {
    switch (rand() % 4) {
        case 0: return Special::Wink;
        case 1: return Special::Dance;
        case 2: return Special::Wobble;
        default: return Special::Surprised;
    }
}

// Big, chunky, cute sewn-button eye. Used by the Coraline layout.
void drawButtonEyes(float centerY) {
    constexpr float R = 0.20f;
    constexpr float HOLE_D = 0.05f;
    uint32_t colorShine = display::rgb(255, 250, 240);

    for (int side = -1; side <= 1; side += 2) {
        float cx = 0.5f + side * 0.22f;

        display::fillCircleNorm(cx, centerY, R, colorFace);
        display::drawCircleNorm(cx, centerY, R, 0.014f, colorRing);
        display::fillCircleNorm(cx - R * 0.35f, centerY - R * 0.35f, R * 0.28f, colorShine);

        display::fillCircleNorm(cx - HOLE_D, centerY - HOLE_D, 0.018f, colorRing);
        display::fillCircleNorm(cx + HOLE_D, centerY - HOLE_D, 0.018f, colorRing);
        display::fillCircleNorm(cx - HOLE_D, centerY + HOLE_D, 0.018f, colorRing);
        display::fillCircleNorm(cx + HOLE_D, centerY + HOLE_D, 0.018f, colorRing);
    }
}

// Small weather icon, top-left, mirroring the WiFi-lost dot on the right.
// Deliberately generic vector shapes so it sits quietly on top of any skin.
void drawWeatherIcon() {
    weather::Condition c = weather::current();
    if (c == weather::Condition::Unknown) return;

    // Kept within the physical round bezel's visible circle (radius ~0.47
    // from center) — anything further out gets clipped away by the screen.
    constexpr float cx = 0.22f, cy = 0.22f;
    uint32_t ink = display::rgb(200, 200, 200);

    switch (c) {
        case weather::Condition::Clear:
            display::fillCircleNorm(cx, cy, 0.022f, ink);
            display::drawLineNorm(cx - 0.04f, cy, cx - 0.03f, cy, 0.004f, ink);
            display::drawLineNorm(cx + 0.03f, cy, cx + 0.04f, cy, 0.004f, ink);
            display::drawLineNorm(cx, cy - 0.04f, cx, cy - 0.03f, 0.004f, ink);
            display::drawLineNorm(cx, cy + 0.03f, cx, cy + 0.04f, 0.004f, ink);
            break;
        case weather::Condition::Cloudy:
            display::fillCircleNorm(cx - 0.015f, cy, 0.020f, ink);
            display::fillCircleNorm(cx + 0.015f, cy + 0.006f, 0.020f, ink);
            break;
        case weather::Condition::Rain:
            display::fillCircleNorm(cx, cy - 0.006f, 0.024f, ink);
            display::drawLineNorm(cx - 0.012f, cy + 0.02f, cx - 0.02f, cy + 0.045f, 0.004f, ink);
            display::drawLineNorm(cx + 0.012f, cy + 0.02f, cx + 0.004f, cy + 0.045f, 0.004f, ink);
            break;
        case weather::Condition::Snow:
            display::fillCircleNorm(cx, cy - 0.006f, 0.024f, ink);
            display::fillCircleNorm(cx - 0.014f, cy + 0.035f, 0.006f, ink);
            display::fillCircleNorm(cx + 0.014f, cy + 0.035f, 0.006f, ink);
            break;
        case weather::Condition::Storm:
            display::fillCircleNorm(cx, cy - 0.006f, 0.024f, ink);
            display::drawLineNorm(cx + 0.005f, cy + 0.018f, cx - 0.01f, cy + 0.035f, 0.005f, ink);
            display::drawLineNorm(cx - 0.01f, cy + 0.035f, cx + 0.008f, cy + 0.038f, 0.005f, ink);
            display::drawLineNorm(cx + 0.008f, cy + 0.038f, cx - 0.006f, cy + 0.055f, 0.005f, ink);
            break;
        case weather::Condition::Fog:
            display::drawLineNorm(cx - 0.03f, cy - 0.012f, cx + 0.03f, cy - 0.012f, 0.004f, ink);
            display::drawLineNorm(cx - 0.03f, cy, cx + 0.03f, cy, 0.004f, ink);
            display::drawLineNorm(cx - 0.03f, cy + 0.012f, cx + 0.03f, cy + 0.012f, 0.004f, ink);
            break;
        default:
            break;
    }
}

// Full custom layout: dark vignette, sewn-button eyes (or a stitched-shut
// line when blinking), and a dashed stitched-seam mouth. No ring — this
// skin deliberately breaks from the generic TE-dial template.
void drawCoralineFace(bool blink) {
    uint32_t vignette = display::rgb(6, 5, 8);
    for (float r = 0.49f; r > 0.40f; r -= 0.02f) {
        display::drawCircleNorm(0.5f, 0.5f, r, 0.02f, vignette);
    }

    constexpr float eyesY = 0.40f;
    if (blink) {
        for (int side = -1; side <= 1; side += 2) {
            float cx = 0.5f + side * 0.22f;
            display::drawLineNorm(cx - 0.14f, eyesY, cx + 0.14f, eyesY, 0.012f, colorRing);
        }
        lastEyesText = "- -";
    } else {
        drawButtonEyes(eyesY);
        lastEyesText = "x x";
    }

    constexpr float mouthY = 0.68f;
    for (int i = -2; i <= 2; i++) {
        float cx = 0.5f + i * 0.06f;
        display::drawLineNorm(cx - 0.02f, mouthY, cx + 0.02f, mouthY, 0.010f, colorRing);
    }
    lastMouthText = "~~~";
}

void drawGlitchAscii(uint32_t now) {
    static const char* GARBAGE[] = { "#%", "&?", "X0", "1@", "!$", "^&" };
    const char* eyesText = GARBAGE[(now / 40) % 6];
    const char* mouthText = "##";

    constexpr float off = 0.012f;
    display::drawTextCenteredNorm(0.5f - off, 0.40f, 0.20f, eyesText, colorGlitchCyan);
    display::drawTextCenteredNorm(0.5f + off, 0.40f, 0.20f, eyesText, colorGlitchMagenta);
    display::drawTextCenteredNorm(0.5f, 0.40f, 0.20f, eyesText, colorFace);
    display::drawTextCenteredNorm(0.5f, 0.60f, 0.16f, mouthText, colorFace);

    int bars = 1 + (rand() % 2);
    for (int i = 0; i < bars; i++) {
        float y = (rand() % 100) / 100.0f;
        float h = 0.02f + (rand() % 20) / 1000.0f;
        display::fillRectNorm(0.0f, y, 1.0f, h, (i % 2 == 0) ? colorGlitchCyan : colorGlitchMagenta);
    }

    lastEyesText = eyesText;
    lastMouthText = mouthText;
}

// TV-static snow: a scatter of small random-colored blocks over the frame.
void drawGlitchStatic() {
    uint32_t palette[] = { colorGlitchCyan, colorGlitchMagenta, colorFace, colorBg };
    for (int i = 0; i < 55; i++) {
        float x = (rand() % 100) / 100.0f;
        float y = (rand() % 100) / 100.0f;
        float w = 0.02f + (rand() % 40) / 1000.0f;
        float h = 0.01f + (rand() % 25) / 1000.0f;
        display::fillRectNorm(x, y, w, h, palette[rand() % 4]);
    }
    lastEyesText = "####";
    lastMouthText = "####";
}

// Real cat photo, dropped in as a random glitch payload.
void drawGlitchCat() {
    display::pushImageCenteredNorm(CAT_IMG, 160, 0.92f);
    lastEyesText = "=^.^=";
    lastMouthText = "meow";
}

// The actual 1998 Google homepage screenshot — the "wtf is happening" glitch.
void drawGlitchGoogle() {
    display::pushImageCenteredNorm(GOOGLE_IMG, 160, 0.92f);
    lastEyesText = "[___]";
    lastMouthText = "search";
}

// Full custom layout: normally shows quiet "code" glyphs, then randomly
// bursts into one of several fake-bug payloads — a glitchy ASCII flicker,
// TV static, a stray cat, or a nonsense search bar.
void drawGlitchFace(uint32_t now, bool blink) {
    if (now >= nextGlitchCheckMs) {
        nextGlitchCheckMs = now + 400;
        if (!glitchBurst && (rand() % 100) < 25) {
            glitchBurst = true;
            int roll = rand() % 20;
            if (roll < 14)      { glitchPayload = GlitchPayload::Ascii;  glitchUntilMs = now + 120 + rand() % 120; }
            else if (roll < 17) { glitchPayload = GlitchPayload::Static; glitchUntilMs = now + 150 + rand() % 150; }
            else if (roll < 19) { glitchPayload = GlitchPayload::Cat;    glitchUntilMs = now + 500 + rand() % 300; }
            else                { glitchPayload = GlitchPayload::Google; glitchUntilMs = now + 500 + rand() % 300; }
        }
    }
    if (glitchBurst && now >= glitchUntilMs) {
        glitchBurst = false;
    }

    if (!glitchBurst) {
        const char* eyesText = blink ? "- -" : idleEyes;
        display::drawTextCenteredNorm(0.5f, 0.40f, 0.20f, eyesText, colorFace);
        display::drawTextCenteredNorm(0.5f, 0.60f, 0.16f, idleMouth, colorFace);
        lastEyesText = eyesText;
        lastMouthText = idleMouth;
        return;
    }

    switch (glitchPayload) {
        case GlitchPayload::Ascii:  drawGlitchAscii(now); break;
        case GlitchPayload::Static: drawGlitchStatic();   break;
        case GlitchPayload::Cat:    drawGlitchCat();       break;
        case GlitchPayload::Google: drawGlitchGoogle();    break;
    }
}

}

namespace face {

void setSkin(int index) {
    if (index < 0) index = 0;
    if (index >= SKIN_COUNT) index = SKIN_COUNT - 1;

    skinIndex = index;
    const SkinDef& s = SKINS[index];

    colorBg   = display::rgb(s.bgR, s.bgG, s.bgB);
    colorFace = display::rgb(s.faceR, s.faceG, s.faceB);
    colorRing = display::rgb(s.accentR, s.accentG, s.accentB);
    ringEnabled = s.ring;
    layout = s.layout;
    idleEyes = s.eyesIdle;
    idleMouth = s.mouthIdle;

    prefs.putInt("skin", skinIndex);

    if (!(seenMask & (1u << index))) {
        seenMask |= (1u << index);
        prefs.putUInt("seen", seenMask);
    }
}

int getSkin() { return skinIndex; }
int getSkinCount() { return SKIN_COUNT; }
const char* getSkinName(int index) {
    if (index < 0 || index >= SKIN_COUNT) return "";
    return SKINS[index].name;
}

int seenCount() {
    int n = 0;
    for (int i = 0; i < SKIN_COUNT; i++) {
        if (seenMask & (1u << i)) n++;
    }
    return n;
}

void celebrateUnlock(const char* skinName) {
    snprintf(bannerText, sizeof(bannerText), "%s", skinName);
    bannerUntilMs = millis() + 2500;
}

void begin() {
    colorGlitchCyan = display::rgb(60, 220, 255);
    colorGlitchMagenta = display::rgb(255, 60, 180);

    prefs.begin("knomi", false);
    seenMask = prefs.getUInt("seen", 0);
    int saved = prefs.getInt("skin", 0);
    setSkin(saved);

    scheduleNextBlink(0);
    scheduleNextSpecial(0);
}

void triggerSpecial() {
    uint32_t now = millis();
    special = pickSpecial();
    specialUntilMs = now + ((special == Special::Dance || special == Special::Wobble) ? 1600 : 900);
}

void update(uint32_t now) {
    // --- state transitions (shared by every skin/layout) ---
    if (special == Special::None) {
        if (!blinking && now >= nextBlinkMs) {
            blinking = true;
            blinkUntilMs = now + 110;
        }
        if (blinking && now >= blinkUntilMs) {
            blinking = false;
            scheduleNextBlink(now);
        }
        if (now >= nextSpecialMs) {
            special = pickSpecial();
            uint32_t duration = (special == Special::Dance || special == Special::Wobble) ? 1600 : 900;
            specialUntilMs = now + duration;
        }
    } else if (now >= specialUntilMs) {
        special = Special::None;
        scheduleNextSpecial(now);
        scheduleNextBlink(now);
    }

    display::beginFrame();
    display::fillScreenNorm(colorBg);

    if (special != Special::None) {
        // Special animations always use the shared generic template, even
        // on custom-layout skins, so the long-press easter egg stays
        // meaningful everywhere.
        const char* eyesText = "o o";
        const char* mouthText = "-";
        float eyesX = 0.5f, eyesY = 0.40f;
        float mouthX = 0.5f, mouthY = 0.60f;
        float eyeGlyphH = 0.20f;
        float mouthGlyphH = 0.16f;

        switch (special) {
            case Special::Wink:
                eyesText = "o -";
                mouthText = "u";
                break;
            case Special::Surprised: {
                eyesText = "O O";
                mouthText = "o";
                float pulse = 1.0f + 0.06f * sinf((now % 400) / 400.0f * 6.2831f);
                eyeGlyphH *= pulse;
                break;
            }
            case Special::Dance: {
                eyesText = "^ ^";
                mouthText = ((now / 150) % 2 == 0) ? "u" : "w";
                float phase = (now % 600) / 600.0f * 6.2831f;
                float bounce = sinf(phase) * 0.035f;
                eyesY += bounce;
                mouthY += bounce;
                eyesX += sinf(phase * 0.5f) * 0.02f;
                mouthX -= sinf(phase * 0.5f) * 0.02f;
                break;
            }
            case Special::Wobble: {
                eyesText = "@ @";
                mouthText = "~";
                float phase = (now % 500) / 500.0f * 6.2831f;
                eyesX += sinf(phase) * 0.03f;
                mouthX += sinf(phase + 1.0f) * 0.03f;
                break;
            }
            default:
                break;
        }

        if (ringEnabled) {
            display::drawCircleNorm(0.5f, 0.5f, 0.47f, 0.006f, colorRing);
        }
        display::drawTextCenteredNorm(eyesX, eyesY, eyeGlyphH, eyesText, colorFace);
        display::drawTextCenteredNorm(mouthX, mouthY, mouthGlyphH, mouthText, colorFace);
        lastEyesText = eyesText;
        lastMouthText = mouthText;

    } else {
        switch (layout) {
            case LAYOUT_CORALINE:
                drawCoralineFace(blinking);
                break;
            case LAYOUT_GLITCH:
                drawGlitchFace(now, blinking);
                break;
            default: {
                if (ringEnabled) {
                    display::drawCircleNorm(0.5f, 0.5f, 0.47f, 0.006f, colorRing);
                }
                const char* eyesText = blinking ? "- -" : idleEyes;
                display::drawTextCenteredNorm(0.5f, 0.40f, 0.20f, eyesText, colorFace);
                display::drawTextCenteredNorm(0.5f, 0.60f, 0.16f, idleMouth, colorFace);
                lastEyesText = eyesText;
                lastMouthText = idleMouth;
                break;
            }
        }
    }

    // Small red dot, top-right, whenever WiFi isn't connected — independent
    // of skin/layout so it's always visible as a quiet "offline" signal.
    // Kept within the round bezel's visible circle (radius ~0.47 from center).
    if (WiFi.status() != WL_CONNECTED) {
        display::fillCircleNorm(0.78f, 0.22f, 0.025f, display::rgb(220, 40, 40));
    }
    drawWeatherIcon();

    if (bannerUntilMs > now) {
        display::fillRectNorm(0.0f, 0.44f, 1.0f, 0.16f, display::rgb(0, 0, 0));
        display::drawTextCenteredNorm(0.5f, 0.52f, 0.07f, "NEW SKIN", display::rgb(255, 220, 60));
        display::drawTextCenteredNorm(0.5f, 0.60f, 0.06f, bannerText, display::rgb(255, 220, 60));
    }

    display::endFrame(colorBg);

    static uint8_t lastBrightness = 255;
    uint8_t targetBrightness = computeBrightness();
    if (targetBrightness != lastBrightness) {
        display::setBrightness(targetBrightness);
        lastBrightness = targetBrightness;
    }
}

Snapshot getSnapshot() {
    return Snapshot{ lastEyesText, lastMouthText };
}

}
