#include "skins.h"

// Trailing field is the ASCII-art animation index — only read when layout is
// LAYOUT_ASCIIART, 0 everywhere else.
const SkinDef SKINS[] = {
    // Cat first: the active skin is persisted in NVS and defaults to 0,
    // so this is what shows with nothing clicked.
    { "Cat",           4,   8,   18,  90,   175, 255, 30,  70,  130, "/\\_/\\", "w", false, LAYOUT_ASCIIART, 0 },
    { "TE Orange",     12,  11,  10,  255,  90,   0,  60,  38,  20, "o o", "-", true,  LAYOUT_DEFAULT,  0 },
    { "Mono",          244, 242, 238, 17,   17,  17,  200, 200, 200,"o o", "-", false, LAYOUT_DEFAULT,  0 },
    { "Terminal",      3,   20,  10,  51,   255, 119, 13,  61,  31, "^ ^", "_", true,  LAYOUT_DEFAULT,  0 },
    { "Kawaii",        255, 242, 230, 43,   35,  32,  232, 196, 160,"o o", "u", false, LAYOUT_DEFAULT,  0 },
    { "Dial",          20,  20,  20,  255,  255, 255, 255, 90,  0,  "^ ^", "w", true,  LAYOUT_DEFAULT,  0 },
    { "Blocks",        232, 228, 218, 17,   17,  17,  255, 90,  0,  "# #", "=", false, LAYOUT_DEFAULT,  0 },
    { "Coraline",      18,  16,  20,  20,   20,  25,  60,  55,  70, "x x", "~", false, LAYOUT_CORALINE, 0 },
    { "Night Garden",  24,  20,  50,  230,  230, 240, 90,  70,  140,". .", "_", true,  LAYOUT_DEFAULT,  0 },
    { "OP-1 Field",    238, 233, 222, 30,   30,  30,  180, 170, 150,"o o", "-", false, LAYOUT_DEFAULT,  0 },
    { "PO Grid",       10,  10,  10,  230,  30,  30,  60,  10,  10, "# #", "=", true,  LAYOUT_DEFAULT,  0 },
    { "Game Boy",      15,  56,  15,  155,  188, 15,  48,  98,  48, "o o", "_", false, LAYOUT_DEFAULT,  0 },
    { "Vaporwave",     20,  10,  40,  255,  60,  180, 60,  220, 220,"^ ^", "w", true,  LAYOUT_DEFAULT,  0 },
    { "Nokia 3310",    196, 207, 161, 30,   40,  20,  120, 130, 90, "# #", "-", false, LAYOUT_DEFAULT,  0 },
    { "Cassette",      238, 225, 200, 200,  80,  20,  120, 60,  20, "+ +", "-", true,  LAYOUT_DEFAULT,  0 },
    { "Cyber HUD",     5,   8,   10,  60,   220, 255, 20,  80,  90, "[ ]", "=", true,  LAYOUT_DEFAULT,  0 },
    { "Claymation",    235, 220, 195, 60,   40,  30,  180, 140, 100,"o o", "w", false, LAYOUT_DEFAULT,  0 },
    { "Glitch",        6,   6,   8,   230,  230, 235, 40,  40,  50, "0 0", "//",false, LAYOUT_GLITCH,   0 },

    // --- animated ASCII art ---
    { "Hearth",        10,  6,   4,   255,  140, 40,  120, 50,  10, ") (",  "=", false, LAYOUT_ASCIIART, 1 },
    { "Heartbeat",     8,   4,   8,   240,  50,  80,  100, 20,  40, "<3",   "^", false, LAYOUT_ASCIIART, 2 },
    { "Coffee",        18,  14,  10,  225,  200, 165, 110, 85,  60, ") (",  "_", false, LAYOUT_ASCIIART, 3 },
    // Ajouter uniquement ici : l index de skin est persiste en NVS, donc
    // reordonner la table reassignerait silencieusement le skin choisi.
    { "Fox",           14,  8,   4,   255,  150, 60,  120, 60,  20, "/\_/\\", "w", false, LAYOUT_ASCIIART, 4 },
    { "Fox photo",     12,  8,   6,   255,  165, 90,  120, 70,  30, "/\_/\\", "w", false, LAYOUT_ASCIIART, 5 },
};

const int SKIN_COUNT = sizeof(SKINS) / sizeof(SKINS[0]);
