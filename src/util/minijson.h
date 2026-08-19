#pragma once
#include <Arduino.h>

// Field extraction for flat JSON objects.
//
// Every document exchanged with this device is a handful of scalars with no
// nesting, so a real parser would cost more flash than it saves. These
// helpers are deliberately forgiving: a missing or malformed field simply
// returns false and leaves the caller's variable untouched, which is what
// lets an older client talk to newer firmware without breaking.
namespace minijson {

inline bool findValue(const String& src, const char* key, int& pos) {
    String needle = String("\"") + key + "\"";
    int k = src.indexOf(needle);
    if (k < 0) return false;
    int c = src.indexOf(':', k + needle.length());
    if (c < 0) return false;
    int i = c + 1;
    while (i < (int)src.length() && (src[i] == ' ' || src[i] == '\t')) i++;
    pos = i;
    return true;
}

inline bool getInt(const String& src, const char* key, long& out) {
    int i;
    if (!findValue(src, key, i)) return false;
    int start = i;
    if (i < (int)src.length() && (src[i] == '-' || src[i] == '+')) i++;
    bool digits = false;
    while (i < (int)src.length() && isDigit(src[i])) { i++; digits = true; }
    if (!digits) return false;
    out = src.substring(start, i).toInt();
    return true;
}

inline bool getBool(const String& src, const char* key, bool& out) {
    int i;
    if (!findValue(src, key, i)) return false;
    if (src.startsWith("true", i))  { out = true;  return true; }
    if (src.startsWith("false", i)) { out = false; return true; }
    return false;
}

// Reads a quoted string, honouring backslash escapes for quote and
// backslash. WiFi passphrases routinely contain punctuation, so dropping
// out at the first backslash would corrupt them silently.
inline bool getString(const String& src, const char* key, String& out) {
    int i;
    if (!findValue(src, key, i)) return false;
    if (i >= (int)src.length() || src[i] != '"') return false;
    i++;
    String v;
    while (i < (int)src.length()) {
        char c = src[i];
        if (c == '\\') {
            if (i + 1 >= (int)src.length()) return false;
            char e = src[i + 1];
            if (e == 'n')      v += '\n';
            else if (e == 't') v += '\t';
            else if (e == 'r') v += '\r';
            else               v += e;   // covers \" and \\ and anything else
            i += 2;
            continue;
        }
        if (c == '"') { out = v; return true; }
        v += c;
        i++;
    }
    return false;   // unterminated
}

// Escapes a value for embedding in a JSON string literal.
inline String escape(const String& s) {
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if ((uint8_t)c < 0x20) continue;    // drop other control bytes
        else out += c;
    }
    return out;
}

}
