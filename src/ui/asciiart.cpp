#include "asciiart.h"
#include "../assets/cat_ascii.h"

// The hand-typed art below is 11 columns wide: at glyphH 0.10 that is
// textSize 3 on a 240px panel, so 18x24 px cells, 198px across.
//
// The cat is different — it is generated at the panel's full character
// resolution (40x30, textSize 1) by assets/gen_cat_ascii.py and lives in
// src/assets/cat_ascii.cpp. Hand-typing shaded art at that density is not
// practical; see the header of that script for the pipeline.
//
// See asciiart.h for the two layout rules every frame here obeys.

namespace {

using asciiart::Frame;

// ---------------------------------------------------------------------------
// Fire — flames flicker over a fixed hearth. Four frames, no repeats: the
// point is that no two consecutive frames agree.
// ---------------------------------------------------------------------------
const char* const FIRE_A[] = {
    "     )     ",
    "    ) (    ",
    "   ( ) )   ",
    "  ) ( ( )  ",
    "  ( ) ) (  ",
    "   \\ | /   ",
    "  =[___]=  ",
};
const char* const FIRE_B[] = {
    "     (     ",
    "    ( )    ",
    "   ) ( (   ",
    "  ( ) ) (  ",
    "  ) ( ( )  ",
    "   \\ | /   ",
    "  =[___]=  ",
};
const char* const FIRE_C[] = {
    "    ) )    ",
    "   ( ( )   ",
    "  ) ) ( (  ",
    "   ( ) )   ",
    "  ( ) ( )  ",
    "   \\ | /   ",
    "  =[___]=  ",
};
const char* const FIRE_D[] = {
    "    ( (    ",
    "   ) ) (   ",
    "  ( ( ) )  ",
    "   ) ( (   ",
    "  ) ( ) (  ",
    "   \\ | /   ",
    "  =[___]=  ",
};
const char* const FIRE_EMBER_A[] = {
    "           ",
    "           ",
    "           ",
    "     .     ",
    "    . .    ",
    "   \\ | /   ",
    "  =[___]=  ",
};
const char* const FIRE_EMBER_B[] = {
    "           ",
    "           ",
    "           ",
    "     '     ",
    "    . .    ",
    "   \\ | /   ",
    "  =[___]=  ",
};

const Frame FIRE_FRAMES[] = {
    { FIRE_A, 7 }, { FIRE_B, 7 }, { FIRE_C, 7 }, { FIRE_D, 7 },
};
const Frame FIRE_SLEEP[] = {
    { FIRE_EMBER_A, 7 }, { FIRE_EMBER_B, 7 },
};

// ---------------------------------------------------------------------------
// Heart — one beat, then a rest. The pause is made of repeated small frames,
// which is what turns a pulse into a heartbeat.
// ---------------------------------------------------------------------------
const char* const HEART_S[] = {
    "   __ __   ",
    "  /  V  \\  ",
    "  \\     /  ",
    "   \\   /   ",
    "    \\_/    ",
};
const char* const HEART_M[] = {
    "  _-- --_  ",
    " /   V   \\ ",
    " \\       / ",
    "  \\     /  ",
    "   \\_,_/   ",
};
const char* const HEART_FLAT[] = {
    "           ",
    "           ",
    " _____^____",
    "           ",
    "           ",
};

const Frame HEART_FRAMES[] = {
    { HEART_S, 5 }, { HEART_M, 5 }, { HEART_S, 5 }, { HEART_M, 5 },
    { HEART_S, 5 }, { HEART_S, 5 }, { HEART_S, 5 }, { HEART_S, 5 },
    { HEART_S, 5 }, { HEART_S, 5 },
};
const Frame HEART_SLEEP[] = {
    { HEART_S, 5 }, { HEART_S, 5 }, { HEART_S, 5 }, { HEART_S, 5 },
    { HEART_S, 5 }, { HEART_S, 5 }, { HEART_S, 5 },
    { HEART_FLAT, 5 },
};

// ---------------------------------------------------------------------------
// Coffee — a mug with rising steam. Desk-toy appropriate.
// ---------------------------------------------------------------------------
const char* const CUP_A[] = {
    "    ) (    ",
    "   ( ) )   ",
    "    ) (    ",
    "  .-----.  ",
    "  |     |  ",
    "  |     |  ",
    "  '-----'  ",
};
const char* const CUP_B[] = {
    "    ( )    ",
    "   ) ( (   ",
    "    ( )    ",
    "  .-----.  ",
    "  |     |  ",
    "  |     |  ",
    "  '-----'  ",
};
const char* const CUP_C[] = {
    "    ) )    ",
    "   ( ( )   ",
    "    ) (    ",
    "  .-----.  ",
    "  |     |  ",
    "  |     |  ",
    "  '-----'  ",
};
const char* const CUP_COLD[] = {
    "           ",
    "           ",
    "           ",
    "  .-----.  ",
    "  |     |  ",
    "  |     |  ",
    "  '-----'  ",
};

const Frame CUP_FRAMES[] = {
    { CUP_A, 7 }, { CUP_B, 7 }, { CUP_C, 7 }, { CUP_B, 7 },
};
const Frame CUP_SLEEP[] = {
    { CUP_COLD, 7 },
};

}

namespace asciiart {

const Anim ANIMS[] = {
    // Generated 40x30 art: glyphH 0.0333 is textSize 1, and lineMul 1.0
    // makes 30 rows tile the panel exactly.
    { "cat",    assets::CAT_ASCII, (uint8_t)assets::CAT_ASCII_COUNT,
                assets::CAT_ASCII_SLEEP, (uint8_t)assets::CAT_ASCII_SLEEP_COUNT,
                110, 0.03333f, 1.0f },
    // Hand-typed 11-column art.
    { "fire",   FIRE_FRAMES,   4, FIRE_SLEEP,   2, 130, 0.10f, 1.02f },
    { "heart",  HEART_FRAMES, 10, HEART_SLEEP,  8, 110, 0.10f, 1.02f },
    { "coffee", CUP_FRAMES,    4, CUP_SLEEP,    1, 260, 0.10f, 1.02f },
};

const int ANIM_COUNT = sizeof(ANIMS) / sizeof(ANIMS[0]);

}
