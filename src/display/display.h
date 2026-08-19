#pragma once
#include <cstdint>

// Thin, panel-agnostic drawing surface. UI code (src/ui/*) talks only to
// this API — never to LovyanGFX or GPIO pins directly — so that porting to
// a different panel/bus (see lgfx_config.hpp) never touches UI code.
//
// Coordinates are normalized floats in [0.0, 1.0] across the drawable
// square, so eye geometry is resolution-independent (240x240 today,
// 480x480 on the final target).
namespace display {

void begin();

// Panel backlight, 0-255. Driven by the PWM light on GPIO 2.
void setBrightness(uint8_t level);

int widthPx();
int heightPx();

// Call once per frame before drawing, and once after to push to panel.
void beginFrame();
void endFrame(uint32_t backgroundColor);

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b);

// All coordinates/radii are normalized to [0.0, 1.0] of the screen's
// shorter side, with (0,0) at the drawable square's top-left.
void fillScreenNorm(uint32_t color);
void fillCircleNorm(float cx, float cy, float r, uint32_t color);
void fillEllipseNorm(float cx, float cy, float rx, float ry, uint32_t color);
void fillRectNorm(float x, float y, float w, float h, uint32_t color);
void drawCircleNorm(float cx, float cy, float r, float thicknessNorm, uint32_t color);
void drawLineNorm(float x1, float y1, float x2, float y2, float thicknessNorm, uint32_t color);
void fillTriangleNorm(float x1, float y1, float x2, float y2, float x3, float y3, uint32_t color);

// Blits a square RGB565 image (side x side pixels), centered on the screen,
// scaled to occupy roughly sizeNorm of the shorter screen dimension.
void pushImageCenteredNorm(const uint16_t* data, int side, float sizeNorm);

// Draws text centered on (cx, cy) using the built-in bitmap font.
// glyphHeightNorm is the rendered glyph cell height as a fraction of the
// screen's shorter side (the font is blocky/pixelated by nature, which
// fits a retro-ASCII look at any resolution).
void drawTextCenteredNorm(float cx, float cy, float glyphHeightNorm, const char* text, uint32_t color);

}
