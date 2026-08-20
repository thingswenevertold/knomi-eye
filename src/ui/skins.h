#pragma once
#include <cstdint>

// Most skins are a palette + idle glyph pair drawn through the shared
// generic face template. A few need a genuinely different composition —
// those set `layout` to something other than LAYOUT_DEFAULT, and face.cpp
// dispatches to a bespoke draw routine for them instead of the template.
enum SkinLayout : uint8_t {
    LAYOUT_DEFAULT = 0,   // ring + centered eyes/mouth glyphs (the common look)
    LAYOUT_CORALINE = 1,  // vignette, big sewn-button eyes, dashed stitch mouth, no ring
    LAYOUT_GLITCH = 2,    // ASCII glyphs with chromatic-aberration glitch bursts
};

struct SkinDef {
    const char* name;
    uint8_t bgR, bgG, bgB;
    uint8_t faceR, faceG, faceB;
    uint8_t accentR, accentG, accentB;
    const char* eyesIdle;
    const char* mouthIdle;
    bool ring;
    uint8_t layout;

    // XP threshold at which this skin is "celebrated" as newly unlocked.
    // Not a hard lock — every skin stays freely selectable at any time via
    // the dashboard; this only gates the one-time unlock banner/progress.
    uint32_t unlockXp;
};

extern const SkinDef SKINS[];
extern const int SKIN_COUNT;
