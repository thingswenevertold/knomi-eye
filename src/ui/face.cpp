#include "face.h"
#include "../display/display.h"
#include "skins.h"
#include "mood.h"
#include "tuning.h"
#include "asciiart.h"
#include "../assets/cat.h"
#include "../assets/google.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <cstdlib>
#include <cmath>
#include <cstring>

// Multi-skin ASCII face. Most skins share one generic template (ring +
// centered eyes/mouth glyphs); a couple of skins (Coraline, Glitch) use a
// completely different composition, dispatched by layout in update().

namespace {

enum class Special { None, Wink, Dance, Wobble, Surprised, Angry };

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
uint8_t animIndex = 0;

// Energy-scaled clock for ASCII-art playback. Accumulating scaled deltas
// (rather than scaling millis() directly) keeps the loop continuous when
// the mood changes mid-animation.
uint32_t asciiPhaseMs = 0;
uint32_t asciiLastMs = 0;

Preferences prefs;

bool blinking = false;
uint32_t blinkUntilMs = 0;
uint32_t nextBlinkMs = 0;

Special special = Special::None;
uint32_t specialUntilMs = 0;
uint32_t nextSpecialMs = 0;

const char* lastEyesText = "o o";
const char* lastMouthText = "-";

enum class GlitchPayload { Ascii, Static, Cat, Google };

bool glitchBurst = false;
uint32_t glitchUntilMs = 0;
uint32_t nextGlitchCheckMs = 0;
GlitchPayload glitchPayload = GlitchPayload::Ascii;

uint32_t randRange(uint32_t lo, uint32_t hi) {
    return lo + (rand() % (hi - lo + 1));
}

void scheduleNextBlink(uint32_t now) {
    uint32_t lo, hi;
    mood::blinkInterval(lo, hi);
    nextBlinkMs = now + randRange(lo, hi);
}

void scheduleNextSpecial(uint32_t now) {
    uint32_t lo, hi;
    mood::specialInterval(lo, hi);
    nextSpecialMs = now + randRange(lo, hi);
}

// Reserved for physical interaction: only the friendly reactions, so a
// button press reads as "hello" rather than as a random twitch.
Special pickPleased() {
    return (rand() % 2 == 0) ? Special::Wink : Special::Dance;
}

Special pickSpecial() {
    switch (rand() % 4) {
        case 0: return Special::Wink;
        case 1: return Special::Dance;
        case 2: return Special::Wobble;
        default: return Special::Surprised;
    }
}

// The big moves need longer on screen than a wink to read as deliberate.
uint32_t specialDuration(Special s) {
    if (s == Special::Angry) return 2500;   // une humeur, pas un tic
    return (s == Special::Dance || s == Special::Wobble) ? 1600 : 900;
}

// Duree et nombre de tours en cours. Une mimique joue une fois par defaut ;
// le reveil, lui, rejoue la surprise plusieurs fois de suite pour qu'elle
// dure sans etre ralentie au point de paraitre figee.
uint32_t specialSpanMs = 900;
uint8_t specialLoops = 1;

void startSpecial(Special s, uint32_t now, uint32_t spanMs, uint8_t loops) {
    blinking = false;
    special = s;
    specialSpanMs = spanMs ? spanMs : specialDuration(s);
    specialLoops = loops ? loops : 1;
    specialUntilMs = now + specialSpanMs;
}

// The animations a remote control can ask for by name. Keeping the table in
// the firmware and letting clients read it back means a phone builds its
// buttons from what the device can actually play, instead of shipping a copy
// that drifts the moment an animation is added here.
struct NamedAnim { const char* name; Special kind; };
const NamedAnim NAMED_ANIMS[] = {
    { "wink",      Special::Wink      },
    { "angry",     Special::Angry     },
    { "dance",     Special::Dance     },
    { "wobble",    Special::Wobble    },
    { "surprised", Special::Surprised },
};
const int NAMED_ANIM_COUNT = sizeof(NAMED_ANIMS) / sizeof(NAMED_ANIMS[0]);

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

// Multi-line ASCII picture. Each row is drawn as its own centered line —
// asciiart.cpp guarantees every row of a frame has identical length, which
// is what keeps them aligned with each other.
// Deplacement applique a l'art ASCII, en unites normalisees.
//
// L'animation de l'art se contente d'echanger des frames entieres, ce qui se
// lit comme un clignotement plutot que comme un mouvement. Ces offsets sont
// des fonctions continues du temps : a 90 images par seconde l'image glisse
// pixel par pixel, et le personnage bouge au lieu de sauter.
// 0 au debut de l'animation nommee en cours, 1 a sa fin. Vaut 1 quand
// aucune ne tourne. Le deplacement et la mimique s'y referent tous les deux,
// ce qui les garde en phase par construction.
float specialProgress(uint32_t now) {
    if (special == Special::None) return 1.0f;
    const uint32_t dur = specialSpanMs ? specialSpanMs : specialDuration(special);
    if (dur == 0) return 1.0f;
    uint32_t remain = (specialUntilMs > now) ? (specialUntilMs - now) : 0;
    if (remain > dur) remain = dur;
    return 1.0f - (float)remain / (float)dur;
}

struct ArtMotion {
    float dx;     // translation horizontale
    float dy;     // translation verticale
    float lean;   // inclinaison : appliquee en haut, nulle en bas, donc le
                  // personnage bascule sur sa base au lieu de glisser
};

ArtMotion artMotion(uint32_t now) {
    ArtMotion m{0.0f, 0.0f, 0.0f};

    // Respiration permanente, tres faible. C'est ce qui distingue une image
    // fixe d'une creature au repos, et ca ne coute rien.
    const float e = mood::energy();
    m.dy += 0.006f * sinf(now * 0.0016f) * (0.35f + 0.65f * e);

    if (special == Special::None) return m;
    const float t = specialProgress(now);

    switch (special) {
        case Special::Dance: {
            // Deux sauts sur la duree, avec un balancement lateral.
            const float phase = t * 6.2831f * 2.0f;
            m.dx += 0.050f * sinf(phase);
            m.dy -= 0.020f * fabsf(sinf(phase));
            break;
        }
        case Special::Wobble: {
            // Bascule d'un cote puis de l'autre, pivot en bas.
            m.lean += 0.065f * sinf(t * 6.2831f * 1.5f);
            break;
        }
        case Special::Wink: {
            // Un plongeon bref, aller-retour sur toute la duree.
            m.dy += 0.022f * sinf(t * 3.1416f);
            break;
        }
        case Special::Surprised: {
            // Sursaut vers le haut, puis retour amorti.
            m.dy -= 0.055f * sinf(t * 3.1416f * 3.0f) * (1.0f - t);
            break;
        }
        default:
            break;
    }
    return m;
}

void drawAsciiArtFace(uint32_t now, bool direct = false) {
    const asciiart::Anim& a = asciiart::ANIMS[animIndex];

    const bool asleep = (mood::get() == mood::State::Asleep);
    const asciiart::Frame* frames = a.frames;
    uint8_t count = a.frameCount;
    if (asleep && a.sleepFrames != nullptr && a.sleepFrameCount > 0) {
        frames = a.sleepFrames;
        count = a.sleepFrameCount;
    }
    // Une animation nommee prend la main sur la boucle d'attente : l'art joue
    // sa propre mimique, une seule fois, en phase avec le deplacement.
    int forced = -1;
    if (special != Special::None && a.expressions != nullptr) {
        const asciiart::Expressions& ex = *a.expressions;
        const asciiart::Frame* set = nullptr;
        uint8_t setCount = 0;
        switch (special) {
            case Special::Wink:
                set = ex.wink;     setCount = ex.winkCount;     break;
            case Special::Surprised:
                set = ex.surprise; setCount = ex.surpriseCount; break;
            case Special::Dance:
            case Special::Wobble:
                set = ex.happy;    setCount = ex.happyCount;    break;
            case Special::Angry:
                set = ex.angry;    setCount = ex.angryCount;    break;
            default:
                break;
        }
        if (set != nullptr && setCount > 0) {
            frames = set;
            count = setCount;
            // specialLoops tours du jeu sur la duree : un seul pour une
            // mimique breve, plusieurs pour une expression qui doit tenir.
            const float adv = specialProgress(now) * setCount * specialLoops;
            forced = (int)adv % (int)setCount;
            if (forced < 0) forced = 0;
        }
    }

    if (count == 0) return;

    uint32_t dt = now - asciiLastMs;
    if (dt > 250) dt = 250;   // never fast-forward after a stall
    asciiLastMs = now;
    float e = mood::energy();
    if (e < 0.05f) e = 0.05f;
    asciiPhaseMs += (uint32_t)(dt * e * tuning::speedScale());

    const uint16_t frameMs = a.frameMs ? a.frameMs : 200;
    const asciiart::Frame& f =
        (forced >= 0) ? frames[forced] : frames[(asciiPhaseMs / frameMs) % count];
    if (f.rowCount == 0) return;

    const ArtMotion m = artMotion(now);
    // La frame est rendue une fois en bitmap 1 bit conserve en PSRAM, puis
    // collee par bandes — une par rangee, ce qui conserve l'inclinaison.
    // Le rognage de marges d'avant devient inutile : coller une bande coute
    // pareil qu'elle soit vide ou pleine, et environ dix fois moins que de
    // re-rendre la rangee en texte.
    if (direct) {
        display::drawArtDirect(f.rows, f.rows, f.rowCount, a.glyphH,
                               (a.lineMul > 0.0f ? a.lineMul : 1.02f),
                               m.dx, m.dy, m.lean, colorFace, colorBg);
    } else {
        display::drawArtCached(f.rows, f.rows, f.rowCount, a.glyphH,
                               (a.lineMul > 0.0f ? a.lineMul : 1.02f),
                               m.dx, m.dy, m.lean, colorFace, colorBg);
    }

    lastEyesText = idleEyes;
    lastMouthText = asleep ? "zzz" : idleMouth;
}

// Sleepy 'z's drifting up and to the right. Drawn over whatever the active
// layout produced, so every skin sleeps the same way. Coordinates stay well
// inside the circle at their furthest point.
void drawSleepZs(uint32_t now) {
    for (int i = 0; i < 3; i++) {
        float phase = ((now / 12 + i * 400) % 3600) / 3600.0f;
        float x = 0.60f + phase * 0.14f;
        float y = 0.34f - phase * 0.16f;
        float h = 0.045f + i * 0.010f;
        display::drawTextCenteredNorm(x, y, h, "z", colorFace);
    }
}

// The skin supplies a palette, but a live override from the dashboard or
// from BLE outranks it. Both paths land here.
void applyPalette() {
    const SkinDef& s = SKINS[skinIndex];
    const tuning::State& t = tuning::get();
    if (t.colorOverride) {
        colorBg   = display::rgb(t.bgR, t.bgG, t.bgB);
        colorFace = display::rgb(t.fgR, t.fgG, t.fgB);
        colorRing = display::rgb(t.accR, t.accG, t.accB);
    } else {
        colorBg   = display::rgb(s.bgR, s.bgG, s.bgB);
        colorFace = display::rgb(s.faceR, s.faceG, s.faceB);
        colorRing = display::rgb(s.accentR, s.accentG, s.accentB);
    }
}

}

namespace face {

void setSkin(int index) {
    if (index < 0) index = 0;
    if (index >= SKIN_COUNT) index = SKIN_COUNT - 1;

    skinIndex = index;
    const SkinDef& s = SKINS[index];

    // Let the UI's colour pickers follow the preset, unless the user has
    // already overridden them.
    tuning::adoptSkinColors(s.bgR, s.bgG, s.bgB,
                            s.faceR, s.faceG, s.faceB,
                            s.accentR, s.accentG, s.accentB);
    applyPalette();
    ringEnabled = s.ring;
    layout = s.layout;
    idleEyes = s.eyesIdle;
    idleMouth = s.mouthIdle;
    animIndex = (s.anim < asciiart::ANIM_COUNT) ? s.anim : 0;

    prefs.putInt("skin", skinIndex);
}

int getSkin() { return skinIndex; }
int getSkinCount() { return SKIN_COUNT; }
const char* getSkinName(int index) {
    if (index < 0 || index >= SKIN_COUNT) return "";
    return SKINS[index].name;
}

bool isSkinHidden(int index) {
    if (index < 0 || index >= SKIN_COUNT) return true;
    return SKINS[index].hidden;
}

void begin() {
    // Without this, rand() starts from the same seed on every boot and the
    // creature replays an identical script of blinks, specials and glitch
    // payloads every time it powers on. esp_random() is hardware-backed.
    srand(esp_random());

    mood::begin();
    asciiLastMs = millis();

    colorGlitchCyan = display::rgb(60, 220, 255);
    colorGlitchMagenta = display::rgb(255, 60, 180);

    prefs.begin("knomi", false);
    int saved = prefs.getInt("skin", 0);
    // Le skin enregistre peut avoir ete retire entre-temps. Son creneau existe
    // encore — c'est tout l'interet — mais l'afficher montrerait une creature
    // que l'utilisateur croit supprimee. On retombe sur le premier visible.
    if (saved < 0 || saved >= SKIN_COUNT || SKINS[saved].hidden) {
        saved = 0;
        for (int i = 0; i < SKIN_COUNT; i++) {
            if (!SKINS[i].hidden) { saved = i; break; }
        }
    }
    setSkin(saved);

    scheduleNextBlink(0);
    scheduleNextSpecial(0);
}

void triggerSpecial() {
    const uint32_t now = millis();
    const Special s = pickSpecial();
    startSpecial(s, now, specialDuration(s), 1);
}

bool playAnim(const char* name) {
    if (name == nullptr) return false;
    for (int i = 0; i < NAMED_ANIM_COUNT; i++) {
        if (strcmp(name, NAMED_ANIMS[i].name) != 0) continue;
        // Same interruption rules as a button long-press: whatever is on
        // screen gives way, a blink in flight included.
        const Special s = NAMED_ANIMS[i].kind;
        startSpecial(s, millis(), specialDuration(s), 1);
        return true;
    }
    return false;
}

int getAnimCount() { return NAMED_ANIM_COUNT; }

const char* getAnimName(int index) {
    if (index < 0 || index >= NAMED_ANIM_COUNT) return "";
    return NAMED_ANIMS[index].name;
}

void notifyInteraction(uint32_t now) {
    mood::notifyInteraction(now);

    // Being touched always gets an acknowledgement, and it interrupts
    // whatever was on screen — including sleep.
    const Special s = pickPleased();
    startSpecial(s, now, specialDuration(s), 1);
    scheduleNextBlink(now);
}

const char* moodName() {
    return mood::name();
}

void refreshPalette() {
    const SkinDef& s = SKINS[skinIndex];
    tuning::adoptSkinColors(s.bgR, s.bgG, s.bgB,
                            s.faceR, s.faceG, s.faceB,
                            s.accentR, s.accentG, s.accentB);
    applyPalette();
}

void update(uint32_t now) {
    const mood::State before = mood::get();
    mood::update(now);
    const mood::State after = mood::get();

    // Les basculements d'humeur declenchent une reaction, une seule fois au
    // passage. Sans cette memoire de l'etat precedent, la reaction se
    // relancerait a chaque image tant que l'humeur dure.
    if (after != before) {
        if (after == mood::State::Bored) {
            // Delaissee assez longtemps pour s'en agacer.
            startSpecial(Special::Angry, now, specialDuration(Special::Angry), 1);
        } else if (before == mood::State::Asleep) {
            // Reveil en sursaut : la surprise boucle cinq secondes. Elle
            // rejoue plusieurs fois plutot que d'etre etiree, sinon elle
            // ralentirait au point de paraitre figee.
            startSpecial(Special::Surprised, now, 5000, 5);
        }
    }

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
        // Rien de spontane pendant le sommeil : une creature endormie qui
        // danse toute seule n'a aucun sens, et c'est pourtant ce qui se
        // passait — ce declencheur n'avait pas de garde. Le minuteur continue
        // d'avancer pour qu'au reveil la premiere animation ne parte pas
        // immediatement.
        if (mood::get() == mood::State::Asleep) {
            scheduleNextSpecial(now);
        } else if (now >= nextSpecialMs) {
            special = pickSpecial();
            uint32_t duration = (special == Special::Dance || special == Special::Wobble) ? 1600 : 900;
            specialUntilMs = now + duration;
        }
    } else if (now >= specialUntilMs) {
        special = Special::None;
        scheduleNextSpecial(now);
        scheduleNextBlink(now);
    }

    // L'art eveille possede le cadre entier et rien ne se dessine dessus :
    // il part directement au panneau, sans sprite du tout. Le sommeil garde
    // la voie sprite, ses z se superposant a l'art ; les mimiques aussi,
    // elles passent par le gabarit generique.
    if (special == Special::None && layout == LAYOUT_ASCIIART
        && mood::get() != mood::State::Asleep) {
        drawAsciiArtFace(now, true);
        return;
    }

    display::beginFrame();
    // Le layout ASCII s'en passe : drawArtCached ecrit chaque rangee du
    // cadre exactement une fois, fond compris. Remplir d'abord doublait le
    // cout memoire de l'image entiere.
    const bool artFullFrame =
        (special == Special::None) && (layout == LAYOUT_ASCIIART);
    if (!artFullFrame) {
        display::fillScreenNorm(colorBg);
    }

    // Les skins en art ASCII gardent leur image pendant une animation : elle
    // EST le personnage, la remplacer par "o o" le fait disparaitre au profit
    // d'un visage generique. Le mouvement est porte par artMotion() a la
    // place. Les autres layouts continuent d'utiliser le template partage.
    if (special != Special::None && layout != LAYOUT_ASCIIART) {
        // Special animations use the shared generic template, so the
        // long-press easter egg stays meaningful on those skins.
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
                float bounce = sinf(phase) * 0.035f * mood::energy();
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
            case LAYOUT_ASCIIART:
                drawAsciiArtFace(now);
                break;
            default: {
                if (ringEnabled) {
                    display::drawCircleNorm(0.5f, 0.5f, 0.47f, 0.006f, colorRing);
                }
                // The mood can override the skin's resting expression: a
                // sleeping creature has its eyes shut whatever skin is on,
                // and a creature with no network looks for it.
                const char* eyesText;
                const char* mouthText;
                switch (mood::get()) {
                    case mood::State::Asleep:
                        eyesText = "- -";
                        mouthText = "~";
                        break;
                    case mood::State::Lost:
                        eyesText = blinking ? "- -" : "? ?";
                        mouthText = "~";
                        break;
                    default:
                        eyesText = blinking ? "- -" : idleEyes;
                        mouthText = idleMouth;
                        break;
                }
                display::drawTextCenteredNorm(0.5f, 0.40f, 0.20f, eyesText, colorFace);
                display::drawTextCenteredNorm(0.5f, 0.60f, 0.16f, mouthText, colorFace);
                lastEyesText = eyesText;
                lastMouthText = mouthText;
                break;
            }
        }
    }

    if (special == Special::None && mood::get() == mood::State::Asleep) {
        drawSleepZs(now);
    }

    display::endFrame(colorBg);
}

Snapshot getSnapshot() {
    return Snapshot{ lastEyesText, lastMouthText };
}

}
