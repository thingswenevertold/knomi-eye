#include "wifiprov.h"
#include "../util/minijson.h"

#include <Preferences.h>
#include <WiFi.h>

namespace {

Preferences prefs;
String ssid;
String pass;

// Cached so a scan result survives the poll that reads it, and so repeated
// reads do not re-walk the driver's list.
String lastScan = "{\"nets\":[]}";

bool waitForConnect(uint32_t timeoutMs) {
    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(150);
    }
    return WiFi.status() == WL_CONNECTED;
}

}

namespace wifiprov {

void begin() {
    prefs.begin("wifiprov", false);
    ssid = prefs.getString("ssid", "");
    pass = prefs.getString("pass", "");
}

bool hasStored() { return ssid.length() > 0; }
String storedSsid() { return ssid; }

bool joinStored(uint32_t timeoutMs) {
    if (!hasStored()) return false;
    WiFi.begin(ssid.c_str(), pass.c_str());
    return waitForConnect(timeoutMs);
}

bool join(const String& newSsid, const String& newPass, uint32_t timeoutMs) {
    if (newSsid.length() == 0) return false;

    // Keep the old credentials until the new ones are proven, so a typo does
    // not strand a device that was working a moment ago.
    WiFi.disconnect(false, true);
    delay(100);
    WiFi.begin(newSsid.c_str(), newPass.c_str());

    if (!waitForConnect(timeoutMs)) {
        WiFi.disconnect(false, true);
        if (hasStored()) {
            WiFi.begin(ssid.c_str(), pass.c_str());
            waitForConnect(timeoutMs);
        }
        return false;
    }

    ssid = newSsid;
    pass = newPass;
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    return true;
}

void forget() {
    ssid = "";
    pass = "";
    prefs.remove("ssid");
    prefs.remove("pass");
}

void startScan() {
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;
    WiFi.scanDelete();
    WiFi.scanNetworks(true /* async */, false /* show hidden */);
}

bool scanBusy() { return WiFi.scanComplete() == WIFI_SCAN_RUNNING; }

String scanResultJson() {
    const int n = WiFi.scanComplete();
    if (n < 0) return lastScan;   // running or never started: keep the last one

    String j = "{\"nets\":[";
    // The radio is shared with BLE and the display loop; a full list of a
    // busy office would also overflow a comfortable BLE payload. Strongest
    // 12 is plenty to pick yours out of.
    const int limit = n < 12 ? n : 12;
    for (int i = 0; i < limit; i++) {
        if (i) j += ",";
        j += "{\"ssid\":\"" + minijson::escape(WiFi.SSID(i)) + "\"";
        j += ",\"rssi\":" + String(WiFi.RSSI(i));
        j += ",\"lock\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true");
        j += "}";
    }
    j += "],\"more\":" + String(n > limit ? n - limit : 0) + "}";

    lastScan = j;
    WiFi.scanDelete();
    return j;
}

String statusJson() {
    const bool up = WiFi.status() == WL_CONNECTED;
    String j = "{\"up\":" + String(up ? "true" : "false");
    j += ",\"ssid\":\"" + minijson::escape(up ? WiFi.SSID() : ssid) + "\"";
    j += ",\"stored\":" + String(hasStored() ? "true" : "false");
    j += ",\"ip\":\"" + String(up ? WiFi.localIP().toString() : String("")) + "\"";
    j += ",\"rssi\":" + String(up ? WiFi.RSSI() : 0);
    j += "}";
    return j;
}

}
