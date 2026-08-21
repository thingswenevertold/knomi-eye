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
    LAYOUT_ASCIIART = 3,  // multi-line looping ASCII picture, from asciiart.cpp
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
    // Index into asciiart::ANIMS. Only meaningful for LAYOUT_ASCIIART;
    // ignored (and conventionally 0) for every other layout.
    uint8_t anim;
    // Créature retirée : le créneau est conservé, mais elle disparaît des
    // interfaces.
    //
    // On ne peut pas simplement supprimer une entrée : l'index de skin est
    // persisté en NVS, donc retirer la ligne N ferait glisser d'un cran tout
    // ce qui suit, et une carte réglée sur le skin 7 se retrouverait en
    // silence sur un autre. Le créneau reste donc occupé et seul l'affichage
    // change. L'art, lui, cesse d'être référencé et l'éditeur de liens
    // l'élimine — la place en flash est bien rendue.
    //
    // Dernier champ : les entrées écrites avant lui restent valides en
    // initialisation agrégée, où un membre omis vaut false.
    bool hidden;
};

extern const SkinDef SKINS[];
extern const int SKIN_COUNT;
