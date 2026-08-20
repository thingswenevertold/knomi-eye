#include "wifiprov.h"
#include "../util/minijson.h"

#include <Preferences.h>
#include <WiFi.h>

namespace {

Preferences prefs;

String ssids[wifiprov::MAX_NETS];
String passes[wifiprov::MAX_NETS];
int nets = 0;

String lastScan = "{\"nets\":[]}";

// Cles NVS courtes : "s0".."s5", "p0".."p5". La limite NVS est de 15
// caracteres, mais court reste court.
String keyS(int i) { return String("s") + i; }
String keyP(int i) { return String("p") + i; }

void persist() {
    prefs.putInt("n", nets);
    for (int i = 0; i < nets; i++) {
        prefs.putString(keyS(i).c_str(), ssids[i]);
        prefs.putString(keyP(i).c_str(), passes[i]);
    }
    // Purge les entrees au-dela du compte, sinon un oubli laisserait
    // trainer une cle dans la flash.
    for (int i = nets; i < wifiprov::MAX_NETS; i++) {
        prefs.remove(keyS(i).c_str());
        prefs.remove(keyP(i).c_str());
    }
}

bool waitForConnect(uint32_t timeoutMs) {
    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(150);
    }
    return WiFi.status() == WL_CONNECTED;
}

int indexOf(const String& ssid) {
    for (int i = 0; i < nets; i++) {
        if (ssids[i] == ssid) return i;
    }
    return -1;
}

// Ajoute ou met a jour, sans tester la connexion.
void rememberImpl(const String& ssid, const String& pass) {
    const int at = indexOf(ssid);
    if (at >= 0) {
        passes[at] = pass;
    } else if (nets < wifiprov::MAX_NETS) {
        ssids[nets] = ssid;
        passes[nets] = pass;
        nets++;
    } else {
        // Plein : evince le plus ancien en decalant vers le haut.
        for (int i = 1; i < wifiprov::MAX_NETS; i++) {
            ssids[i - 1] = ssids[i];
            passes[i - 1] = passes[i];
        }
        ssids[wifiprov::MAX_NETS - 1] = ssid;
        passes[wifiprov::MAX_NETS - 1] = pass;
    }
    persist();
}

}

namespace wifiprov {

void begin() {
    prefs.begin("wifiprov", false);

    nets = prefs.getInt("n", -1);

    if (nets < 0) {
        // Migration depuis l'ancien format a reseau unique, pour ne pas
        // perdre le reseau deja provisionne au premier flash de cette
        // version.
        nets = 0;
        String legacy = prefs.getString("ssid", "");
        if (legacy.length() > 0) {
            ssids[0] = legacy;
            passes[0] = prefs.getString("pass", "");
            nets = 1;
            persist();
            prefs.remove("ssid");
            prefs.remove("pass");
        }
        return;
    }

    if (nets > MAX_NETS) nets = MAX_NETS;
    for (int i = 0; i < nets; i++) {
        ssids[i] = prefs.getString(keyS(i).c_str(), "");
        passes[i] = prefs.getString(keyP(i).c_str(), "");
    }
}

int count() { return nets; }

String ssidAt(int index) {
    if (index < 0 || index >= nets) return "";
    return ssids[index];
}

bool knows(const String& ssid) { return indexOf(ssid) >= 0; }

bool joinBest(uint32_t perNetworkTimeoutMs) {
    if (nets == 0) return false;

    // Scan synchrone : deux a trois secondes, contre plusieurs secondes de
    // timeout par reseau absent si on tentait en aveugle.
    const int seen = WiFi.scanNetworks(false, false);
    if (seen <= 0) {
        WiFi.scanDelete();
        return false;
    }

    // Indices des reseaux connus reperes, tries par puissance decroissante.
    int order[MAX_NETS];
    int rssi[MAX_NETS];
    int found = 0;
    for (int i = 0; i < seen && found < MAX_NETS; i++) {
        const int at = indexOf(WiFi.SSID(i));
        if (at < 0) continue;
        bool already = false;
        for (int k = 0; k < found; k++) {
            if (order[k] == at) { already = true; break; }
        }
        if (already) continue;   // meme SSID vu sur plusieurs bornes
        order[found] = at;
        rssi[found] = WiFi.RSSI(i);
        found++;
    }
    WiFi.scanDelete();
    if (found == 0) return false;

    for (int i = 1; i < found; i++) {
        for (int j = i; j > 0 && rssi[j] > rssi[j - 1]; j--) {
            const int a = order[j]; order[j] = order[j - 1]; order[j - 1] = a;
            const int b = rssi[j];  rssi[j]  = rssi[j - 1];  rssi[j - 1]  = b;
        }
    }

    for (int i = 0; i < found; i++) {
        const int at = order[i];
        WiFi.begin(ssids[at].c_str(), passes[at].c_str());
        if (waitForConnect(perNetworkTimeoutMs)) return true;
        WiFi.disconnect(false, true);
        delay(100);
    }
    return false;
}

bool join(const String& newSsid, const String& newPass, uint32_t timeoutMs) {
    if (newSsid.length() == 0) return false;

    const bool wasUp = (WiFi.status() == WL_CONNECTED);
    const String previous = wasUp ? WiFi.SSID() : String("");

    WiFi.disconnect(false, true);
    delay(100);
    WiFi.begin(newSsid.c_str(), newPass.c_str());

    if (waitForConnect(timeoutMs)) {
        rememberImpl(newSsid, newPass);
        return true;
    }

    // Echec : rien n'est enregistre, et on retourne sur le reseau d'avant
    // pour ne pas couper une carte qui fonctionnait.
    WiFi.disconnect(false, true);
    delay(100);
    const int at = indexOf(previous);
    if (at >= 0) {
        WiFi.begin(ssids[at].c_str(), passes[at].c_str());
        waitForConnect(timeoutMs);
    }
    return false;
}

void remember(const String& ssid, const String& pass) {
    if (ssid.length() == 0) return;
    rememberImpl(ssid, pass);
}

void forget(const String& ssid) {
    if (ssid.length() == 0) {
        nets = 0;
        persist();
        return;
    }
    const int at = indexOf(ssid);
    if (at < 0) return;
    for (int i = at + 1; i < nets; i++) {
        ssids[i - 1] = ssids[i];
        passes[i - 1] = passes[i];
    }
    nets--;
    persist();
}

void startScan() {
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;
    WiFi.scanDelete();
    WiFi.scanNetworks(true, false);
}

bool scanBusy() { return WiFi.scanComplete() == WIFI_SCAN_RUNNING; }

String scanResultJson() {
    const int n = WiFi.scanComplete();
    if (n < 0) return lastScan;

    String j = "{\"nets\":[";
    // La radio est partagee avec le BLE, et la liste complete d'un bureau
    // charge deborderait une notification BLE. Les douze plus forts
    // suffisent a reperer le sien.
    const int limit = n < 12 ? n : 12;
    for (int i = 0; i < limit; i++) {
        if (i) j += ",";
        j += "{\"ssid\":\"" + minijson::escape(WiFi.SSID(i)) + "\"";
        j += ",\"rssi\":" + String(WiFi.RSSI(i));
        j += ",\"lock\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true");
        // Permet a une telecommande de marquer d'un coup d'oeil ce qui est
        // deja enregistre, sans second aller-retour.
        j += ",\"known\":" + String(knows(WiFi.SSID(i)) ? "true" : "false");
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
    j += ",\"ssid\":\"" + minijson::escape(up ? WiFi.SSID() : String("")) + "\"";
    j += ",\"ip\":\"" + String(up ? WiFi.localIP().toString() : String("")) + "\"";
    j += ",\"rssi\":" + String(up ? WiFi.RSSI() : 0);
    j += ",\"known\":[";
    for (int i = 0; i < nets; i++) {
        if (i) j += ",";
        j += "\"" + minijson::escape(ssids[i]) + "\"";
    }
    j += "]}";
    return j;
}

}
