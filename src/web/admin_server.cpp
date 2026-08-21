#include "admin_server.h"
#include "play_page.h"
#include "../ui/face.h"
#include "../ui/mood.h"
#include "../ui/tuning.h"
#include "../diag.h"

#if __has_include("../../include/secrets.h")
#include "../../include/secrets.h"
#else
#error "include/secrets.h missing: copy include/secrets.h.example to include/secrets.h"
#endif

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <Arduino.h>

namespace {

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

uint32_t lastBroadcastMs = 0;
constexpr uint32_t BROADCAST_INTERVAL_MS = 200;

int clampInt(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

bool requireAuth(AsyncWebServerRequest* request) {
    if (!request->authenticate("admin", ADMIN_PASSWORD)) {
        request->requestAuthentication();
        return false;
    }
    return true;
}

String jsonEscape(const char* s) {
    String out;
    for (const char* p = s; *p; p++) {
        if (*p == '"' || *p == '\\') out += '\\';
        out += *p;
    }
    return out;
}

String statusJson() {
    uint32_t totalHeap = ESP.getHeapSize();
    uint32_t freeHeap = ESP.getFreeHeap();
    int heapPct = totalHeap ? (int)(100.0f * (totalHeap - freeHeap) / totalHeap) : 0;

    uint32_t totalPsram = ESP.getPsramSize();
    uint32_t freePsram = ESP.getFreePsram();
    int psramPct = totalPsram ? (int)(100.0f * (totalPsram - freePsram) / totalPsram) : 0;

    // Constantes pour un firmware donne, et ruineuses a calculer :
    // getSketchSize() parcourt l'image en flash avec le cache desactive, ce
    // qui gele les deux coeurs. Mesure a 324 ms par appel — a cinq appels par
    // seconde, la boucle de rendu tombait de 30 a 2,8 images par seconde des
    // qu'un dashboard ou l'app telephone etait ouvert. Autrement dit
    // l'animation s'effondrait precisement quand on la regardait.
    static const uint32_t sketchSize = ESP.getSketchSize();
    static const uint32_t freeSketch = ESP.getFreeSketchSpace();
    uint32_t flashTotal = sketchSize + freeSketch;
    int flashPct = flashTotal ? (int)(100.0f * sketchSize / flashTotal) : 0;

    int rssi = WiFi.RSSI();
    int rssiPct = clampInt((rssi + 90) * 100 / 60, 0, 100); // -90dBm..-30dBm -> 0..100%

    face::Snapshot snap = face::getSnapshot();

    String json = "{";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"rssi_dbm\":" + String(rssi) + ",";
    json += "\"rssi_pct\":" + String(rssiPct) + ",";
    json += "\"uptime_s\":" + String(millis() / 1000) + ",";
    json += "\"heap_pct\":" + String(heapPct) + ",";
    json += "\"psram_pct\":" + String(psramPct) + ",";
    json += "\"flash_pct\":" + String(flashPct) + ",";
    json += "\"eyes\":\"" + jsonEscape(snap.eyes) + "\",";
    json += "\"mouth\":\"" + jsonEscape(snap.mouth) + "\",";
    json += "\"last_button\":\"" + jsonEscape(diag::getButtonEvent()) + "\",";
    json += "\"screen\":\"" + jsonEscape(diag::getScreen()) + "\",";
    json += "\"fps\":" + String(diag::getFps(), 1) + ",";
    json += "\"draw_us\":" + String(diag::getDrawUs()) + ",";
    json += "\"push_us\":" + String(diag::getPushUs()) + ",";
    json += "\"net_us\":" + String(diag::getNetUs()) + ",";
    json += "\"total_us\":" + String(diag::getTotalUs()) + ",";
    json += "\"ble_us\":" + String(diag::getBleUs()) + ",";
    json += "\"ota_us\":" + String(diag::getOtaUs()) + ",";
    json += "\"admin_us\":" + String(diag::getAdminUs()) + ",";
    json += "\"clean_us\":" + String(diag::getCleanUs()) + ",";
    json += "\"json_us\":" + String(diag::getJsonUs()) + ",";
    json += "\"send_us\":" + String(diag::getSendUs()) + ",";
    json += "\"mood\":\"" + jsonEscape(face::moodName()) + "\",";
    json += "\"skin\":" + String(face::getSkin());
    json += "}";
    return json;
}

String htmlPage() {
    String html;
    html.reserve(4096);
    html += "<!doctype html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>knomi-eye admin</title><style>";
    html += "body{background:#0c0b0a;color:#ff5a00;font-family:ui-monospace,Consolas,monospace;padding:24px;margin:0}";
    html += "h1{font-size:18px;letter-spacing:.05em;margin:24px 0 16px}";
    html += "h1:first-child{margin-top:0}";
    html += "table{border-collapse:collapse;width:100%;max-width:480px;margin-bottom:8px}";
    html += "td{padding:6px 12px;border-bottom:1px solid #3a2410}";
    html += "td:first-child{color:#c8763a;width:40%}";
    html += "ul{list-style:none;padding:0;max-width:480px}";
    html += "li{padding:6px 12px;border-bottom:1px solid #3a2410;display:flex;justify-content:space-between}";
    html += "a{color:#ff5a00}";
    html += ".mirror{width:180px;height:180px;border-radius:50%;border:2px solid #3a2410;";
    html += "display:flex;flex-direction:column;align-items:center;justify-content:center;gap:8px;";
    html += "background:#0c0b0a;margin-bottom:24px}";
    html += ".mirror span{font-size:28px;letter-spacing:.15em}";
    html += ".bar-row{max-width:480px;margin-bottom:14px}";
    html += ".bar-label{display:flex;justify-content:space-between;font-size:13px;color:#c8763a;margin-bottom:4px}";
    html += ".bar-track{background:#221708;border:1px solid #3a2410;height:14px;border-radius:2px;overflow:hidden}";
    html += ".bar-fill{background:#ff5a00;height:100%;width:0%;transition:width .2s linear}";
    html += ".skins{display:flex;flex-wrap:wrap;gap:8px;max-width:480px;margin-bottom:24px}";
    html += ".skin-btn{background:#221708;border:1px solid #3a2410;color:#c8763a;";
    html += "padding:6px 10px;font-family:inherit;font-size:12px;cursor:pointer;border-radius:2px}";
    html += ".skin-btn.active{background:#ff5a00;color:#0c0b0a;border-color:#ff5a00}";
    html += ".tune{max-width:480px;margin-bottom:24px}";
    html += ".tune label{display:flex;align-items:center;justify-content:space-between;";
    html += "gap:12px;margin-bottom:12px;font-size:13px;color:#c8763a}";
    html += ".tune input[type=color]{width:64px;height:32px;background:none;border:1px solid #3a2410;";
    html += "padding:2px;border-radius:2px;cursor:pointer}";
    html += "input[type=range]{width:100%;accent-color:#ff5a00}";
    html += "</style></head><body>";

    html += "<h1>LIVE</h1>";
    html += "<div class='mirror'><span id='m-eyes'>o o</span><span id='m-mouth'>-</span></div>";

    // Raccourci vers le studio de creatures. Le lien vise localhost a dessein :
    // le studio compile et flashe, il n'ecoute donc que sur la boucle locale.
    // Consequence assumee — depuis un telephone ce lien ne mene nulle part,
    // puisque localhost y designe le telephone. Le texte le dit plutot que de
    // laisser tomber sur un lien mort sans explication.
    html += "<h1>CRÉATURE</h1>";
    html += "<a class='skin-btn' href='http://localhost:8010' target='_blank' "
            "rel='noopener'>DÉTOURER UNE CRÉATURE</a>";
    html += "<p style='font-size:12px;color:#c8763a;max-width:480px;margin:10px 0 24px'>"
            "Ouvre le studio sur la machine depuis laquelle tu lis cette page, "
            "et seulement si <code>py -3.12 tools/studio.py</code> y tourne.</p>";

    html += "<h1>SKIN</h1><div class='skins' id='skins'>";
    for (int i = 0; i < face::getSkinCount(); i++) {
        html += "<button class='skin-btn' data-i='" + String(i) + "' onclick='setSkin(" + String(i) +
                ")'>" + String(face::getSkinName(i)) + "</button>";
    }
    html += "</div>";

    html += "<h1>TUNE</h1><div class='tune'>";
    html += "<label>BACKGROUND<input type='color' id='c-bg'></label>";
    html += "<label>FACE<input type='color' id='c-fg'></label>";
    html += "<label>ACCENT<input type='color' id='c-acc'></label>";
    html += "<div class='bar-row'><div class='bar-label'><span>BRIGHTNESS</span>";
    html += "<span id='bri-val'>--</span></div>";
    html += "<input type='range' min='10' max='255' id='bri'></div>";
    html += "<div class='bar-row'><div class='bar-label'><span>SPEED</span>";
    html += "<span id='spd-val'>--</span></div>";
    html += "<input type='range' min='25' max='400' step='5' id='spd'></div>";
    html += "<button class='skin-btn' id='reset-col'>RESET TO SKIN COLOURS</button>";
    html += "</div>";

    html += "<h1>USAGE</h1>";
    const char* bars[4][2] = {{"heap", "HEAP"}, {"psram", "PSRAM"}, {"flash", "FLASH"}, {"signal", "SIGNAL"}};
    for (auto& b : bars) {
        html += "<div class='bar-row'><div class='bar-label'><span>" + String(b[1]) +
                "</span><span id='" + String(b[0]) + "-pct'>--%</span></div>";
        html += "<div class='bar-track'><div class='bar-fill' id='" + String(b[0]) + "-bar'></div></div></div>";
    }

    html += "<h1>SYSTEM</h1><table>";
    html += "<tr><td>IP</td><td>" + WiFi.localIP().toString() + "</td></tr>";
    html += "<tr><td>Mood</td><td id='mood'>-</td></tr>";
    html += "<tr><td>Uptime</td><td id='uptime'>-</td></tr>";
    html += "<tr><td>Chip</td><td>" + String(ESP.getChipModel()) + " rev" + String(ESP.getChipRevision()) + "</td></tr>";
    html += "<tr><td>Flash size</td><td>" + String(ESP.getFlashChipSize() / (1024 * 1024)) + " MB</td></tr>";
    html += "</table>";

    html += "<h1>FILESYSTEM</h1><ul>";
    File root = LittleFS.open("/");
    if (root) {
        File f = root.openNextFile();
        if (!f) html += "<li>(empty)</li>";
        while (f) {
            html += "<li><span>" + String(f.name()) + "</span><span>" + String(f.size()) +
                    " B &nbsp; <a href='/fs?path=" + String(f.name()) + "'>get</a></span></li>";
            f = root.openNextFile();
        }
    }
    html += "</ul>";

    html += "<script>";
    html += "function setBar(id,pct){document.getElementById(id+'-pct').textContent=pct+'%';";
    html += "document.getElementById(id+'-bar').style.width=pct+'%';}";
    html += "function setSkin(i){tune('skin='+i);}";
    html += "function markSkin(i){document.querySelectorAll('.skin-btn').forEach(function(b){";
    html += "b.classList.toggle('active', b.dataset.i==i);});}";
    html += "function hx(v){return [parseInt(v.substr(1,2),16),parseInt(v.substr(3,2),16),";
    html += "parseInt(v.substr(5,2),16)];}";
    html += "function h2(n){return ('0'+n.toString(16)).slice(-2);}";
    html += "function toHex(a){return '#'+h2(a[0])+h2(a[1])+h2(a[2]);}";
    html += "function showTune(d){";
    html += "document.getElementById('c-bg').value=toHex(d.bg);";
    html += "document.getElementById('c-fg').value=toHex(d.fg);";
    html += "document.getElementById('c-acc').value=toHex(d.accent);";
    html += "document.getElementById('bri').value=d.brightness;";
    html += "document.getElementById('bri-val').textContent=Math.round(d.brightness/255*100)+'%';";
    html += "document.getElementById('spd').value=d.speedPct;";
    html += "document.getElementById('spd-val').textContent=d.speedPct+'%';}";
    html += "function tune(q){fetch('/api/tune?'+q).then(function(r){return r.json();})";
    html += ".then(showTune);}";
    html += "function pushColors(){var b=hx(document.getElementById('c-bg').value),";
    html += "f=hx(document.getElementById('c-fg').value),a=hx(document.getElementById('c-acc').value);";
    html += "tune('bgR='+b[0]+'&bgG='+b[1]+'&bgB='+b[2]+'&fgR='+f[0]+'&fgG='+f[1]+'&fgB='+f[2]";
    html += "+'&accR='+a[0]+'&accG='+a[1]+'&accB='+a[2]);}";
    html += "['c-bg','c-fg','c-acc'].forEach(function(id){";
    html += "document.getElementById(id).addEventListener('change',pushColors);});";
    html += "document.getElementById('bri').addEventListener('change',function(e){";
    html += "tune('brightness='+e.target.value);});";
    html += "document.getElementById('spd').addEventListener('change',function(e){";
    html += "tune('speedPct='+e.target.value);});";
    html += "document.getElementById('reset-col').addEventListener('click',function(){";
    html += "tune('colorOverride=0');});";
    html += "fetch('/api/tune').then(function(r){return r.json();}).then(showTune);";
    html += "var ws=new WebSocket('ws://'+location.host+'/ws');";
    html += "ws.onmessage=function(ev){var d=JSON.parse(ev.data);";
    html += "document.getElementById('m-eyes').textContent=d.eyes;";
    html += "document.getElementById('m-mouth').textContent=d.mouth;";
    html += "setBar('heap',d.heap_pct);setBar('psram',d.psram_pct);";
    html += "setBar('flash',d.flash_pct);setBar('signal',d.rssi_pct);";
    html += "document.getElementById('mood').textContent=d.mood;";
    html += "document.getElementById('uptime').textContent=d.uptime_s+' s';";
    html += "markSkin(d.skin);};";
    html += "</script></body></html>";
    return html;
}

}

namespace admin {

void begin() {
    LittleFS.begin(true); // format on first mount if the partition is blank

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!requireAuth(request)) return;
        request->send(200, "text/html", htmlPage());
    });

    server.on("/fs", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!requireAuth(request)) return;
        if (!request->hasParam("path")) {
            request->send(400, "text/plain", "missing ?path=");
            return;
        }
        String path = request->getParam("path")->value();
        if (!path.startsWith("/")) path = "/" + path;
        if (!LittleFS.exists(path)) {
            request->send(404, "text/plain", "not found");
            return;
        }
        request->send(LittleFS, path, "application/octet-stream");
    });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!requireAuth(request)) return;
        request->send(200, "application/json", statusJson());
    });

    server.on("/api/tune", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!requireAuth(request)) return;

        // Rebuild a JSON document from the query so that HTTP and BLE share
        // a single validate-and-apply path in tuning.cpp.
        static const char* KEYS[] = {
            "bgR", "bgG", "bgB", "fgR", "fgG", "fgB", "accR", "accG", "accB",
            "brightness", "speedPct", "skin",
        };
        String body = "{";
        bool first = true;
        for (auto key : KEYS) {
            if (!request->hasParam(key)) continue;
            if (!first) body += ",";
            body += "\"" + String(key) + "\":" + request->getParam(key)->value();
            first = false;
        }
        if (request->hasParam("colorOverride")) {
            if (!first) body += ",";
            String v = request->getParam("colorOverride")->value();
            body += "\"colorOverride\":";
            body += (v == "1" || v == "true") ? "true" : "false";
        }
        body += "}";

        if (body.length() > 2) tuning::applyJson(body);
        request->send(200, "application/json", tuning::toJson());
    });

    server.on("/api/skin", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!requireAuth(request)) return;
        if (!request->hasParam("index")) {
            request->send(400, "text/plain", "missing ?index=");
            return;
        }
        int idx = request->getParam("index")->value().toInt();
        face::setSkin(idx);
        request->send(200, "text/plain", face::getSkinName(face::getSkin()));
    });

    // The phone app. Same origin as the API it calls, so the browser reuses
    // the Basic Auth credentials without a second prompt.
    server.on("/play", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!requireAuth(request)) return;
        request->send_P(200, "text/html", playpage::HTML);
    });

    server.on("/api/anim", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!requireAuth(request)) return;
        if (!request->hasParam("name")) {
            request->send(400, "text/plain", "missing ?name=");
            return;
        }
        const String name = request->getParam("name")->value();
        if (!face::playAnim(name.c_str())) {
            request->send(404, "text/plain", "unknown animation");
            return;
        }
        request->send(200, "text/plain", name);
    });

    // Exactly what the physical button does, so a tap on a phone and a poke
    // on the device are the same event as far as the mood layer is concerned.
    // Presence du poste de travail. Appelee par un declencheur du
    // planificateur de taches Windows au verrouillage et a la mise en veille,
    // et a l'inverse au reveil. Voir tools/presence-windows.ps1.
    server.on("/api/presence", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!requireAuth(request)) return;
        if (!request->hasParam("away")) {
            request->send(400, "text/plain", "missing ?away=0|1");
            return;
        }
        const bool away = request->getParam("away")->value().toInt() != 0;
        mood::setPcAway(away);
        request->send(200, "text/plain", away ? "away" : "present");
    });

    server.on("/api/pet", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!requireAuth(request)) return;
        face::notifyInteraction(millis());
        request->send(200, "text/plain", face::moodName());
    });

    // What this firmware can do, by name. A client builds its buttons from
    // this instead of shipping a copy that drifts.
    server.on("/api/list", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!requireAuth(request)) return;
        String j = "{\"skinNames\":[";
        for (int i = 0; i < face::getSkinCount(); i++) {
            if (i) j += ",";
            j += "\"" + jsonEscape(face::getSkinName(i)) + "\"";
        }
        j += "],\"animNames\":[";
        for (int i = 0; i < face::getAnimCount(); i++) {
            if (i) j += ",";
            j += "\"" + jsonEscape(face::getAnimName(i)) + "\"";
        }
        j += "]}";
        request->send(200, "application/json", j);
    });

    // Le flux d'etat est derriere le meme Basic Auth que le reste. Sans cela
    // le WebSocket contournait entierement l'authentification du dashboard :
    // une connexion sans le moindre identifiant recevait l'IP, l'humeur et
    // tout le reste. Verifie depuis un poste du reseau.
    ws.setAuthentication("admin", ADMIN_PASSWORD);

    ws.onEvent([](AsyncWebSocket*, AsyncWebSocketClient*, AwsEventType, void*, uint8_t*, size_t) {
        // No client->server messages expected; presence handling not needed.
    });
    server.addHandler(&ws);

    server.begin();
}

void handle(uint32_t nowMs) {
    if (WiFi.status() != WL_CONNECTED) return;

    const uint32_t t0 = micros();
    ws.cleanupClients();
    const uint32_t t1 = micros();

    if (nowMs - lastBroadcastMs < BROADCAST_INTERVAL_MS) {
        diag::setAdminSplit(t1 - t0, 0, 0);
        return;
    }
    lastBroadcastMs = nowMs;

    if (ws.count() > 0) {
        const String payload = statusJson();
        const uint32_t t2 = micros();
        ws.textAll(payload);
        const uint32_t t3 = micros();
        diag::setAdminSplit(t1 - t0, t2 - t1, t3 - t2);
    } else {
        diag::setAdminSplit(t1 - t0, 0, 0);
    }
}

}
