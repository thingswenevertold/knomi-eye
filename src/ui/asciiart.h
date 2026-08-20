#pragma once
#include <cstdint>

// Multi-line animated ASCII art.
//
// The rest of the UI draws one or two huge glyphs ("o o", "-"). This module
// is for actual pictures: a grid of characters, several frames, looping.
//
// Two rules make it work on a round 240x240 panel:
//
//   1. Every row of a frame must be the SAME length. display.h centers each
//      line independently, so ragged rows would slide out of alignment with
//      each other. Pad with spaces.
//   2. Visible glyphs on the top and bottom rows must stay near the middle
//      horizontally, because those rows sit where the circle is narrowest.
//      Trailing spaces cost nothing visually — a wide padded row whose
//      characters are all central is fine.
//
// The renderer lives in face.cpp (drawAsciiArtFace); this file is data only.
namespace asciiart {

struct Frame {
    const char* const* rows;
    uint8_t rowCount;
};

// Mimiques jouees UNE fois pendant une animation nommee, a la place des
// frames d'attente.
//
// C'est ce qui permet a l'image d'etre elle-meme expressive. L'alternative,
// remplacer l'art par un visage generique le temps de l'animation, revient a
// faire disparaitre le personnage de son propre ecran — ce qui est exactement
// ce que faisait la version precedente.
//
// Un art sans mimiques laisse ce pointeur nul : il garde alors sa boucle
// d'attente, et seul le deplacement porte l'animation.
struct Expressions {
    const Frame* wink;       uint8_t winkCount;
    const Frame* surprise;   uint8_t surpriseCount;
    // Joie : partagee par la danse et le balancement, qui different par le
    // mouvement du corps et non par l'expression du visage.
    const Frame* happy;      uint8_t happyCount;
    // Colere : sourcils abaisses, oeil mordu par la paupiere.
    const Frame* angry;      uint8_t angryCount;
};

struct Anim {
    const char* name;
    const Frame* frames;
    uint8_t frameCount;
    // Optional alternate loop used when the creature is asleep. nullptr to
    // reuse the normal frames (slowed down by the mood's energy instead).
    const Frame* sleepFrames;
    uint8_t sleepFrameCount;
    uint16_t frameMs;   // at full energy; mood slows this down
    float glyphH;       // normalized glyph cell height; 0.10 => textSize 3
    // Line pitch as a multiple of glyphH. Dense 30-row art needs exactly
    // 1.0 so 30 rows of 8px tile 240px precisely; coarse art looks better
    // with a little leading.
    float lineMul;
    // nullptr quand l'art n'a pas de mimiques.
    const Expressions* expressions;
};

extern const Anim ANIMS[];
extern const int ANIM_COUNT;

}
