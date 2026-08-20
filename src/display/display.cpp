#include "display.h"
#include "lgfx_config.hpp"

namespace {
    LGFX_KNOMI lcd;
    LGFX_Sprite frame(&lcd);
    int side = 0; // shorter screen dimension, drawable square size
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
    // Il vivait en PSRAM, ce qui coutait tres cher : chaque pixel traverse le
    // bus QSPI deux fois, une fois ecrit pendant le dessin et une fois relu
    // pour le push. Mesure a ~25 images par seconde alors que le SPI du
    // panneau, lui, en autorise pres de 90.
    //
    // On tente donc la RAM interne d'abord. 240*240*2 = 115 Ko, ce qui passe
    // tant que WiFi, BLE et le serveur web laissent la place — et sinon on
    // retombe sur la PSRAM, parce qu'un ecran lent vaut mieux qu'un ecran mort.
    //
    // Essai fait, et abandonne : mis en RAM interne, le sprite n'a rien
    // gagne — cadence identique au dixieme pres — tout en consommant 115 Ko
    // du tas, ce qui a suffi a faire echouer une mise a jour OTA en cours de
    // transfert. Le goulot est ailleurs. La PSRAM reste donc le bon choix
    // ici : elle laisse le tas aux radios et au serveur web.
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
    // startWrite() maintient la transaction ouverte pour toute l'image. L'art
    // ASCII dense dessine 30 chaines de 40 caracteres, soit 1200 glyphes par
    // image : sans cela chaque appel de dessin refait son ouverture et sa
    // fermeture, et c'est cette repetition qui coute, pas les pixels.
    frame.startWrite();
}

void endFrame(uint32_t /*backgroundColor*/) {
    frame.endWrite();
    frame.pushSprite(0, 0);
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

}
