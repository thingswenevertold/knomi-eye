#include "tuning.h"
#include "face.h"
#include "../display/display.h"
#include <Preferences.h>

namespace {

tuning::State st;
Preferences prefs;

// Written on every change. NVS wear is not a concern at the rate a human
// drags a slider, and losing a setting on a power cut would be worse.
void persist() {
    prefs.putBool("ov", st.colorOverride);
    prefs.putUChar("bgR", st.bgR); prefs.putUChar("bgG", st.bgG); prefs.putUChar("bgB", st.bgB);
    prefs.putUChar("fgR", st.fgR); prefs.putUChar("fgG", st.fgG); prefs.putUChar("fgB", st.fgB);
    prefs.putUChar("acR", st.accR); prefs.putUChar("acG", st.accG); prefs.putUChar("acB", st.accB);
    prefs.putUChar("bri", st.brightness);
    prefs.putUShort("spd", st.speedPct);
}

int clampInt(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Minimal scalar extractor: finds "key": <number> in a flat JSON object.
// The documents here are a dozen fields with no nesting, so pulling in a
// parser would cost more flash than it saves.
bool jsonInt(const String& src, const char* key, long& out) {
    String needle = String("\"") + key + "\"";
    int k = src.indexOf(needle);
    if (k < 0) return false;
    int c = src.indexOf(':', k + needle.length());
    if (c < 0) return false;
    int i = c + 1;
    while (i < (int)src.length() && (src[i] == ' ' || src[i] == '\t')) i++;
    int start = i;
    if (i < (int)src.length() && (src[i] == '-' || src[i] == '+')) i++;
    bool digits = false;
    while (i < (int)src.length() && isDigit(src[i])) { i++; digits = true; }
    if (!digits) return false;
    out = src.substring(start, i).toInt();
    return true;
}

bool jsonBool(const String& src, const char* key, bool& out) {
    String needle = String("\"") + key + "\"";
    int k = src.indexOf(needle);
    if (k < 0) return false;
    int c = src.indexOf(':', k + needle.length());
    if (c < 0) return false;
    String rest = src.substring(c + 1, c + 8);
    rest.trim();
    if (rest.startsWith("true"))  { out = true;  return true; }
    if (rest.startsWith("false")) { out = false; return true; }
    return false;
}

}

namespace tuning {

void begin() {
    prefs.begin("tune", false);

    st.colorOverride = prefs.getBool("ov", false);
    st.bgR = prefs.getUChar("bgR", 4);   st.bgG = prefs.getUChar("bgG", 8);   st.bgB = prefs.getUChar("bgB", 18);
    st.fgR = prefs.getUChar("fgR", 90);  st.fgG = prefs.getUChar("fgG", 175); st.fgB = prefs.getUChar("fgB", 255);
    st.accR = prefs.getUChar("acR", 30); st.accG = prefs.getUChar("acG", 70); st.accB = prefs.getUChar("acB", 130);
    st.brightness = prefs.getUChar("bri", 255);
    st.speedPct = prefs.getUShort("spd", 100);

    display::setBrightness(st.brightness);
}

const State& get() { return st; }

void adoptSkinColors(uint8_t bgR, uint8_t bgG, uint8_t bgB,
                     uint8_t fgR, uint8_t fgG, uint8_t fgB,
                     uint8_t accR, uint8_t accG, uint8_t accB) {
    if (st.colorOverride) return;   // the user's choice outranks the preset
    st.bgR = bgR; st.bgG = bgG; st.bgB = bgB;
    st.fgR = fgR; st.fgG = fgG; st.fgB = fgB;
    st.accR = accR; st.accG = accG; st.accB = accB;
}

void setColors(uint8_t bgR, uint8_t bgG, uint8_t bgB,
               uint8_t fgR, uint8_t fgG, uint8_t fgB,
               uint8_t accR, uint8_t accG, uint8_t accB) {
    st.colorOverride = true;
    st.bgR = bgR; st.bgG = bgG; st.bgB = bgB;
    st.fgR = fgR; st.fgG = fgG; st.fgB = fgB;
    st.accR = accR; st.accG = accG; st.accB = accB;
    persist();
    face::refreshPalette();
}

void clearColorOverride() {
    st.colorOverride = false;
    persist();
    face::refreshPalette();   // repopulates the fields from the active skin
}

void setBrightness(uint8_t level) {
    st.brightness = level;
    display::setBrightness(level);
    persist();
}

void setSpeedPct(uint16_t pct) {
    st.speedPct = (uint16_t)clampInt(pct, 25, 400);
    persist();
}

float speedScale() { return st.speedPct / 100.0f; }

String toJson() {
    String j = "{";
    j += "\"colorOverride\":" + String(st.colorOverride ? "true" : "false") + ",";
    j += "\"bg\":[" + String(st.bgR) + "," + String(st.bgG) + "," + String(st.bgB) + "],";
    j += "\"fg\":[" + String(st.fgR) + "," + String(st.fgG) + "," + String(st.fgB) + "],";
    j += "\"accent\":[" + String(st.accR) + "," + String(st.accG) + "," + String(st.accB) + "],";
    j += "\"brightness\":" + String(st.brightness) + ",";
    j += "\"speedPct\":" + String(st.speedPct) + ",";
    j += "\"skin\":" + String(face::getSkin()) + ",";
    j += "\"skinCount\":" + String(face::getSkinCount());
    j += "}";
    return j;
}

bool applyJson(const String& body) {
    bool changed = false;
    long v;

    // Colours arrive as flat "r","g","b" triplets to keep the parser simple.
    long r, g, b;
    bool haveBg  = jsonInt(body, "bgR", r) && jsonInt(body, "bgG", g) && jsonInt(body, "bgB", b);
    if (haveBg) { st.bgR = clampInt(r, 0, 255); st.bgG = clampInt(g, 0, 255); st.bgB = clampInt(b, 0, 255); }

    long r2, g2, b2;
    bool haveFg = jsonInt(body, "fgR", r2) && jsonInt(body, "fgG", g2) && jsonInt(body, "fgB", b2);
    if (haveFg) { st.fgR = clampInt(r2, 0, 255); st.fgG = clampInt(g2, 0, 255); st.fgB = clampInt(b2, 0, 255); }

    long r3, g3, b3;
    bool haveAcc = jsonInt(body, "accR", r3) && jsonInt(body, "accG", g3) && jsonInt(body, "accB", b3);
    if (haveAcc) { st.accR = clampInt(r3, 0, 255); st.accG = clampInt(g3, 0, 255); st.accB = clampInt(b3, 0, 255); }

    if (haveBg || haveFg || haveAcc) {
        st.colorOverride = true;
        changed = true;
    }

    bool ov;
    if (jsonBool(body, "colorOverride", ov) && !ov) {
        st.colorOverride = false;
        changed = true;
    }

    if (jsonInt(body, "brightness", v)) {
        st.brightness = (uint8_t)clampInt(v, 0, 255);
        display::setBrightness(st.brightness);
        changed = true;
    }

    if (jsonInt(body, "speedPct", v)) {
        st.speedPct = (uint16_t)clampInt(v, 25, 400);
        changed = true;
    }

    if (jsonInt(body, "skin", v)) {
        face::setSkin((int)v);       // this calls back into adoptSkinColors
        changed = true;
    }

    if (changed) {
        persist();
        face::refreshPalette();
    }
    return changed;
}

}
