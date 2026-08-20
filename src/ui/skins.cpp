#include "skins.h"

const SkinDef SKINS[] = {
    { "TE Orange",     12,  11,  10,  255,  90,   0,  60,  38,  20, "o o", "-", true,  LAYOUT_DEFAULT,  0   },
    { "Mono",          244, 242, 238, 17,   17,  17,  200, 200, 200,"o o", "-", false, LAYOUT_DEFAULT,  10  },
    { "Terminal",      3,   20,  10,  51,   255, 119, 13,  61,  31, "^ ^", "_", true,  LAYOUT_DEFAULT,  25  },
    { "Kawaii",        255, 242, 230, 43,   35,  32,  232, 196, 160,"o o", "u", false, LAYOUT_DEFAULT,  45  },
    { "Dial",          20,  20,  20,  255,  255, 255, 255, 90,  0,  "^ ^", "w", true,  LAYOUT_DEFAULT,  70  },
    { "Blocks",        232, 228, 218, 17,   17,  17,  255, 90,  0,  "# #", "=", false, LAYOUT_DEFAULT,  100 },
    { "Coraline",      18,  16,  20,  20,   20,  25,  60,  55,  70, "x x", "~", false, LAYOUT_CORALINE, 135 },
    { "Night Garden",  24,  20,  50,  230,  230, 240, 90,  70,  140,". .", "_", true,  LAYOUT_DEFAULT,  175 },
    { "OP-1 Field",    238, 233, 222, 30,   30,  30,  180, 170, 150,"o o", "-", false, LAYOUT_DEFAULT,  220 },
    { "PO Grid",       10,  10,  10,  230,  30,  30,  60,  10,  10, "# #", "=", true,  LAYOUT_DEFAULT,  270 },
    { "Game Boy",      15,  56,  15,  155,  188, 15,  48,  98,  48, "o o", "_", false, LAYOUT_DEFAULT,  325 },
    { "Vaporwave",     20,  10,  40,  255,  60,  180, 60,  220, 220,"^ ^", "w", true,  LAYOUT_DEFAULT,  385 },
    { "Nokia 3310",    196, 207, 161, 30,   40,  20,  120, 130, 90, "# #", "-", false, LAYOUT_DEFAULT,  450 },
    { "Cassette",      238, 225, 200, 200,  80,  20,  120, 60,  20, "+ +", "-", true,  LAYOUT_DEFAULT,  520 },
    { "Cyber HUD",     5,   8,   10,  60,   220, 255, 20,  80,  90, "[ ]", "=", true,  LAYOUT_DEFAULT,  595 },
    { "Claymation",    235, 220, 195, 60,   40,  30,  180, 140, 100,"o o", "w", false, LAYOUT_DEFAULT,  675 },
    { "Glitch",        6,   6,   8,   230,  230, 235, 40,  40,  50, "0 0", "//",false, LAYOUT_GLITCH,   760 },
};

const int SKIN_COUNT = sizeof(SKINS) / sizeof(SKINS[0]);
