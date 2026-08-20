#include "display.h"
#include "lgfx_config.hpp"
#include "../diag.h"

#include <Arduino.h>
#include <cstring>
#include <esp_heap_caps.h>

namespace {
    LGFX_KNOMI lcd;
    LGFX_Sprite frame(&lcd);
    int side = 0; // shorter screen dimension, drawable square size

    // --- cache d'art en bitmap 1 bit -----------------------------------
    // Une entree par frame d'art affichee au moins une fois : 7,2 Ko piece
    // en PSRAM. 224 entrees couvrent tous les jeux actuels avec de la
    // marge, pour ~1,6 Mo au pire — sur les 4 disponibles.
    struct ArtCache {
        const void* key;
        uint8_t* bits;      // side x side, 1 bit/pixel, MSB en tete
        int y0;             // centre de la premiere rangee, en pixels
        int lineH;          // pas de ligne, en pixels
        int cellH;          // hauteur d'une cellule de glyphe, en pixels
        uint8_t rowCount;
    };
    constexpr int MAX_ART_CACHE = 224;
    ArtCache artCache[MAX_ART_CACHE];
    int artCacheCount = 0;

    // Sprite de travail 1 bit, cree au premier besoin, reutilise ensuite.
    LGFX_Sprite* artScratch = nullptr;

    // Tampon de composition d'une bande, en RAM interne : c'est lui qui
    // permet les ecritures en rafale au lieu du pixel-par-pixel.
    // Deux tampons en alternance : l'un se compose pendant que l'autre
    // part en DMA. pushImageDMA attend de lui-meme la fin du transfert
    // precedent, donc composer dans le tampon libere est toujours sur.
    uint16_t* artBand[2] = { nullptr, nullptr };
    int artBandCap = 0;

    bool ensureBand(int cells) {
        if (artBand[0] != nullptr && artBandCap >= cells) return true;
        heap_caps_free(artBand[0]);
        heap_caps_free(artBand[1]);
        artBandCap = cells;
        for (int i = 0; i < 2; i++) {
            artBand[i] = (uint16_t*)heap_caps_malloc(cells * sizeof(uint16_t),
                                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
            if (artBand[i] == nullptr) {
                heap_caps_free(artBand[0]);
                artBand[0] = artBand[1] = nullptr;
                artBandCap = 0;
                return false;
            }
        }
        return true;
    }

    // Retrouve ou construit l'entree de cache d'une frame. La construction
    // rend le texte UNE fois dans un sprite 1 bit, puis re-emballe en
    // MSB-en-tete a la main : l'agencement interne du 1 bit de LovyanGFX
    // n'est pas un contrat public. Ce parcours ne se paie qu'a la premiere
    // apparition de chaque frame.
    ArtCache* artEnsure(const void* key, const char* const* rows, int rowCount,
                        float glyphHeightNorm, float lineMul) {
        for (int i = 0; i < artCacheCount; i++) {
            if (artCache[i].key == key) return &artCache[i];
        }

        if (artCacheCount >= MAX_ART_CACHE) {
            // Cache plein : repartir de zero plutot que d'evincer au hasard.
            // Ne peut arriver qu'en changeant de skin en boucle, et le cout
            // n'est qu'un re-rendu progressif.
            for (int i = 0; i < artCacheCount; i++) heap_caps_free(artCache[i].bits);
            artCacheCount = 0;
        }

        int textSize = (int)(glyphHeightNorm * side / 8.0f + 0.5f);
        if (textSize < 1) textSize = 1;
        const int cellH = textSize * 8;
        const float lineHf = glyphHeightNorm * lineMul * side;
        const float y0f = side * 0.5f - (rowCount - 1) * lineHf * 0.5f;

        if (artScratch == nullptr) {
            artScratch = new LGFX_Sprite(&lcd);
            artScratch->setColorDepth(1);
            artScratch->setPsram(true);
            artScratch->createSprite(side, side);
        }
        artScratch->fillSprite(0);
        artScratch->setTextColor(1);
        artScratch->setTextSize(textSize);
        artScratch->setTextDatum(middle_center);
        for (int i = 0; i < rowCount; i++) {
            artScratch->drawString(rows[i], side / 2, (int)(y0f + i * lineHf + 0.5f));
        }

        const int stride = (side + 7) / 8;
        uint8_t* bits = (uint8_t*)heap_caps_calloc(stride * side, 1, MALLOC_CAP_SPIRAM);
        if (bits == nullptr) bits = (uint8_t*)heap_caps_calloc(stride * side, 1, MALLOC_CAP_8BIT);
        if (bits == nullptr) return nullptr;

        for (int y = 0; y < side; y++) {
            for (int x = 0; x < side; x++) {
                if (artScratch->readPixelValue(x, y)) {
                    bits[y * stride + (x >> 3)] |= (0x80 >> (x & 7));
                }
            }
        }

        ArtCache* e = &artCache[artCacheCount++];
        e->key = key;
        e->bits = bits;
        e->y0 = (int)(y0f + 0.5f);
        e->lineH = (int)(lineHf + 0.5f);
        e->cellH = cellH;
        e->rowCount = (uint8_t)rowCount;
        return e;
    }

    // Compose une bande (h rangees a partir de srcTop, decalee de shift)
    // dans artBand. Rend le nombre de rangees produites.
    int composeBand(uint16_t* band, const ArtCache* e, int srcTop, int h,
                    int shift, uint16_t fg, uint16_t bg) {
        const int stride = (side + 7) / 8;
        for (int y = 0; y < h; y++) {
            const uint8_t* src = e->bits + (srcTop + y) * stride;
            uint16_t* dst = band + y * side;
            for (int x = 0; x < side; x++) {
                const int sx = x - shift;
                const bool on = (sx >= 0 && sx < side)
                                && (src[sx >> 3] & (0x80 >> (sx & 7)));
                dst[x] = on ? fg : bg;
            }
        }
        return h;
    }
}

namespace display {

void begin() {
    lcd.init();
    lcd.setRotation(0);
    lcd.setBrightness(255);

    side = lcd.width() < lcd.height() ? lcd.width() : lcd.height();

    // Tampon plein ecran : il evite le scintillement et fait du push la seule
    // interaction avec le bus.
    //
    // Il vit en PSRAM, et c'est un choix mesure : mis en RAM interne, la
    // cadence n'a pas bouge (elle etait bornee par le pas de la boucle) et
    // les 115 Ko pris au tas ont fait echouer une mise a jour OTA. La PSRAM
    // laisse le tas aux radios et au serveur web.
    //
    // La borne reelle du sprite est sa bande passante : ~11 ms pour ecrire
    // le cadre, ~13 ms pour le relire au push. C'est pour cela que l'art
    // plein cadre passe par drawArtDirect, qui n'y touche pas.
    frame.setPsram(true);
    frame.setColorDepth(16);
    frame.createSprite(lcd.width(), lcd.height());
}

void setBrightness(uint8_t level) { lcd.setBrightness(level); }

int widthPx()  { return lcd.width(); }
int heightPx() { return lcd.height(); }

void beginFrame() {
    // Le sprite est persistant, fillScreenNorm() le nettoie a chaque image.
    //
    // startWrite() maintient la transaction ouverte pour toute l'image :
    // sans cela chaque appel de dessin refait son ouverture et sa
    // fermeture, et c'est cette repetition qui coute, pas les pixels.
    frame.startWrite();
}

void endFrame(uint32_t /*backgroundColor*/) {
    frame.endWrite();
    const uint32_t t0 = micros();
    frame.pushSprite(0, 0);
    diag::setPushUs(micros() - t0);
}

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return frame.color888(r, g, b);
}

void fillScreenNorm(uint32_t color) {
    frame.fillScreen(color);
}

void fillCircleNorm(float cx, float cy, float r, uint32_t color) {
    frame.fillCircle((int)(cx * side), (int)(cy * side), (int)(r * side), color);
}

void fillEllipseNorm(float cx, float cy, float rx, float ry, uint32_t color) {
    frame.fillEllipse((int)(cx * side), (int)(cy * side), (int)(rx * side), (int)(ry * side), color);
}

void fillRectNorm(float x, float y, float w, float h, uint32_t color) {
    frame.fillRect((int)(x * side), (int)(y * side), (int)(w * side), (int)(h * side), color);
}

void drawCircleNorm(float cx, float cy, float r, float thicknessNorm, uint32_t color) {
    int thickness = (int)(thicknessNorm * side);
    if (thickness < 1) thickness = 1;
    frame.drawCircle((int)(cx * side), (int)(cy * side), (int)(r * side), color);
    for (int i = 1; i < thickness; i++) {
        frame.drawCircle((int)(cx * side), (int)(cy * side), (int)(r * side) - i, color);
    }
}

void drawLineNorm(float x1, float y1, float x2, float y2, float thicknessNorm, uint32_t color) {
    int X1 = (int)(x1 * side), Y1 = (int)(y1 * side);
    int X2 = (int)(x2 * side), Y2 = (int)(y2 * side);
    int thickness = (int)(thicknessNorm * side);
    if (thickness < 1) thickness = 1;
    frame.drawLine(X1, Y1, X2, Y2, color);
    for (int i = 1; i < thickness; i++) {
        frame.drawLine(X1 + i, Y1, X2 + i, Y2, color);
        frame.drawLine(X1, Y1 + i, X2, Y2 + i, color);
    }
}

void fillTriangleNorm(float x1, float y1, float x2, float y2, float x3, float y3, uint32_t color) {
    frame.fillTriangle((int)(x1 * side), (int)(y1 * side), (int)(x2 * side), (int)(y2 * side),
                        (int)(x3 * side), (int)(y3 * side), color);
}

void pushImageCenteredNorm(const uint16_t* data, int imgSide, float sizeNorm) {
    LGFX_Sprite src(&frame);
    src.setColorDepth(16);
    src.setBuffer(const_cast<uint16_t*>(data), imgSide, imgSide, 16);

    float scale = (sizeNorm * side) / imgSide;
    src.pushRotateZoom(side / 2, side / 2, 0.0f, scale, scale);
}

void drawTextCenteredNorm(float cx, float cy, float glyphHeightNorm, const char* text, uint32_t color) {
    int textSize = (int)(glyphHeightNorm * side / 8.0f + 0.5f);
    if (textSize < 1) textSize = 1;
    frame.setTextColor(color);
    frame.setTextSize(textSize);
    frame.setTextDatum(middle_center);
    frame.drawString(text, (int)(cx * side), (int)(cy * side));
}

void drawArtCached(const void* key, const char* const* rows, int rowCount,
                   float glyphHeightNorm, float lineMul,
                   float dxNorm, float dyNorm, float leanNorm,
                   uint32_t color, uint32_t bgColor) {
    if (rowCount <= 0) return;
    ArtCache* e = artEnsure(key, rows, rowCount, glyphHeightNorm, lineMul);
    if (e == nullptr) {
        // Sans memoire : on dessine en direct, comme avant le cache.
        const float lineH = glyphHeightNorm * lineMul;
        const float y0 = 0.5f - (rowCount - 1) * lineH * 0.5f + dyNorm;
        for (int i = 0; i < rowCount; i++) {
            const float fromBottom =
                (rowCount > 1) ? (1.0f - (float)i / (float)(rowCount - 1)) : 0.0f;
            drawTextCenteredNorm(0.5f + dxNorm + leanNorm * fromBottom,
                                 y0 + i * lineH, glyphHeightNorm, rows[i], color);
        }
        return;
    }

    // Composition directe dans le tampon du sprite : toutes les voies
    // d'API — drawString, drawBitmap, pushImage — ecrivent ou convertissent
    // pixel par pixel et coutaient 30 a 40 ms l'image. Les ecritures
    // sequentielles, elles, sont absorbees en rafale.
    uint16_t* fb = (uint16_t*)frame.getBuffer();
    if (fb == nullptr) return;

    // L'ordre d'octets natif du sprite est sonde une fois sur un sprite
    // d'un pixel — plus sur qu'une deduction de configuration.
    static LGFX_Sprite* probe = nullptr;
    if (probe == nullptr) {
        probe = new LGFX_Sprite(&lcd);
        probe->setColorDepth(16);
        probe->createSprite(1, 1);
    }
    probe->drawPixel(0, 0, color);
    const uint16_t fg = ((uint16_t*)probe->getBuffer())[0];
    probe->drawPixel(0, 0, bgColor);
    const uint16_t bg = ((uint16_t*)probe->getBuffer())[0];

    const int stride = (side + 7) / 8;

    // Chaque rangee du cadre est ecrite exactement une fois : par une bande
    // si elle en porte une, au fond sinon. C'est ce qui permet a l'appelant
    // de sauter le fillScreen — 115 Ko de PSRAM reecrits pour rien.
    static uint8_t covered[512];
    if (side <= (int)sizeof(covered)) memset(covered, 0, side);

    const int dy = (int)(dyNorm * side + (dyNorm < 0 ? -0.5f : 0.5f));
    for (int i = 0; i < e->rowCount; i++) {
        int top = e->y0 + i * e->lineH - e->cellH / 2;
        int h = e->cellH;
        if (top < 0) { h += top; top = 0; }
        if (top + h > side) h = side - top;
        if (h <= 0) continue;

        const float fromBottom =
            (e->rowCount > 1) ? (1.0f - (float)i / (float)(e->rowCount - 1)) : 0.0f;
        const float xf = (dxNorm + leanNorm * fromBottom) * side;
        const int shift = (int)(xf + (xf < 0 ? -0.5f : 0.5f));

        for (int y = 0; y < h; y++) {
            const int destY = top + dy + y;
            if (destY < 0 || destY >= side) continue;
            const uint8_t* src = e->bits + (top + y) * stride;
            uint16_t* dst = fb + destY * side;
            for (int x = 0; x < side; x++) {
                const int sx = x - shift;
                const bool on = (sx >= 0 && sx < side)
                                && (src[sx >> 3] & (0x80 >> (sx & 7)));
                dst[x] = on ? fg : bg;
            }
            if (destY < (int)sizeof(covered)) covered[destY] = 1;
        }
    }

    for (int y = 0; y < side; y++) {
        if (covered[y]) continue;
        uint16_t* dst = fb + y * side;
        for (int x = 0; x < side; x++) dst[x] = bg;
    }
}

void drawArtDirect(const void* key, const char* const* rows, int rowCount,
                   float glyphHeightNorm, float lineMul,
                   float dxNorm, float dyNorm, float leanNorm,
                   uint32_t color, uint32_t bgColor) {
    if (rowCount <= 0) return;
    ArtCache* e = artEnsure(key, rows, rowCount, glyphHeightNorm, lineMul);
    if (e == nullptr) return;
    if (!ensureBand(side * e->cellH)) return;

    // Couleurs pre-permutees dans l'ordre d'octets du panneau : le DMA
    // envoie alors le tampon tel quel, sans passe de conversion.
    const uint16_t fg = __builtin_bswap16(
        lgfx::color565((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF));
    const uint16_t bg = __builtin_bswap16(
        lgfx::color565((bgColor >> 16) & 0xFF, (bgColor >> 8) & 0xFF, bgColor & 0xFF));

    static uint8_t covered[512];
    if (side <= (int)sizeof(covered)) memset(covered, 0, side);

    const uint32_t t0 = micros();
    lcd.startWrite();

    const int dy = (int)(dyNorm * side + (dyNorm < 0 ? -0.5f : 0.5f));
    for (int i = 0; i < e->rowCount; i++) {
        int top = e->y0 + i * e->lineH - e->cellH / 2;
        int h = e->cellH;
        if (top < 0) { h += top; top = 0; }
        if (top + h > side) h = side - top;
        if (h <= 0) continue;

        const float fromBottom =
            (e->rowCount > 1) ? (1.0f - (float)i / (float)(e->rowCount - 1)) : 0.0f;
        const float xf = (dxNorm + leanNorm * fromBottom) * side;
        const int shift = (int)(xf + (xf < 0 ? -0.5f : 0.5f));

        // Le decalage vertical peut rogner la bande aux bords du cadre.
        int srcTop = top, outH = h, destTop = top + dy;
        if (destTop < 0) { srcTop -= destTop; outH += destTop; destTop = 0; }
        if (destTop + outH > side) outH = side - destTop;
        if (outH <= 0) continue;

        // Alternance : pendant que la bande precedente part en DMA, la
        // suivante se compose dans l'autre tampon. pushImageDMA attend la
        // fin du transfert precedent avant d'engager le sien, ce qui rend
        // la reutilisation sure sans attente explicite.
        uint16_t* band = artBand[i & 1];
        composeBand(band, e, srcTop, outH, shift, fg, bg);
        lcd.pushImageDMA(0, destTop, side, outH, (const lgfx::swap565_t*)band);
        for (int y = destTop; y < destTop + outH && y < (int)sizeof(covered); y++) {
            covered[y] = 1;
        }
    }
    lcd.waitDMA();

    // Rangees restees vierges : fond, par plages contigues.
    int y = 0;
    while (y < side) {
        if (covered[y]) { y++; continue; }
        int y2 = y;
        while (y2 < side && !covered[y2]) y2++;
        lcd.fillRect(0, y, side, y2 - y, bg);
        y = y2;
    }

    lcd.endWrite();
    diag::setPushUs(micros() - t0);
}

}
