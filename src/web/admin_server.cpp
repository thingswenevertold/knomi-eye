#include "admin_server.h"
#include "../ui/face.h"
#include "../diag.h"
#include "../state.h"
#include "../weather.h"
#include "../identity.h"

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
    if (!request->authenticate("admin", identity::adminPassword())) {
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

    uint32_t sketchSize = ESP.getSketchSize();
    uint32_t freeSketch = ESP.getFreeSketchSpace();
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
    json += "\"skin\":" + String(face::getSkin()) + ",";
    json += "\"energy_pct\":" + String(state::energyPercent()) + ",";
    json += "\"xp\":" + String((unsigned long)state::xp()) + ",";
    json += "\"age_s\":" + String((unsigned long)state::ageSeconds()) + ",";
    json += "\"weather\":" + String((int)weather::current()) + ",";
    json += "\"weather_debug\":\"" + jsonEscape(weather::debugInfo()) + "\"";
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
    html += "</style></head><body>";

    html += "<h1>LIVE</h1>";
    html += "<div class='mirror'><span id='m-eyes'>o o</span><span id='m-mouth'>-</span></div>";

    html += "<h1>SKIN <span style='color:#c8763a;font-size:13px'>(" + String(face::seenCount()) +
            "/" + String(face::getSkinCount()) + " discovered)</span></h1><div class='skins' id='skins'>";
    for (int i = 0; i < face::getSkinCount(); i++) {
        html += "<button class='skin-btn' data-i='" + String(i) + "' onclick='setSkin(" + String(i) +
                ")'>" + String(face::getSkinName(i)) + "</button>";
    }
    html += "</div>";

    html += "<h1>TAMAGOTCHI</h1>";
    const char* petBars[1][2] = {{"energy", "ENERGY"}};
    for (auto& b : petBars) {
        html += "<div class='bar-row'><div class='bar-label'><span>" + String(b[1]) +
                "</span><span id='" + String(b[0]) + "-pct'>--%</span></div>";
        html += "<div class='bar-track'><div class='bar-fill' id='" + String(b[0]) + "-bar'></div></div></div>";
    }
    html += "<table><tr><td>XP</td><td id='xp'>-</td></tr>";
    html += "<tr><td>Age</td><td id='age'>-</td></tr></table>";

    html += "<h1>USAGE</h1>";
    const char* bars[4][2] = {{"heap", "HEAP"}, {"psram", "PSRAM"}, {"flash", "FLASH"}, {"signal", "SIGNAL"}};
    for (auto& b : bars) {
        html += "<div class='bar-row'><div class='bar-label'><span>" + String(b[1]) +
                "</span><span id='" + String(b[0]) + "-pct'>--%</span></div>";
        html += "<div class='bar-track'><div class='bar-fill' id='" + String(b[0]) + "-bar'></div></div></div>";
    }

    html += "<h1>SYSTEM</h1><table>";
    html += "<tr><td>IP</td><td>" + WiFi.localIP().toString() + "</td></tr>";
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
    html += "function setSkin(i){fetch('/api/skin?index='+i);}";
    html += "function markSkin(i){document.querySelectorAll('.skin-btn').forEach(function(b){";
    html += "b.classList.toggle('active', b.dataset.i==i);});}";
    html += "var ws=new WebSocket('ws://'+location.host+'/ws');";
    html += "ws.onmessage=function(ev){var d=JSON.parse(ev.data);";
    html += "document.getElementById('m-eyes').textContent=d.eyes;";
    html += "document.getElementById('m-mouth').textContent=d.mouth;";
    html += "setBar('heap',d.heap_pct);setBar('psram',d.psram_pct);";
    html += "setBar('flash',d.flash_pct);setBar('signal',d.rssi_pct);";
    html += "setBar('energy',d.energy_pct);";
    html += "document.getElementById('uptime').textContent=d.uptime_s+' s';";
    html += "document.getElementById('xp').textContent=d.xp;";
    html += "var ageD=Math.floor(d.age_s/86400), ageH=Math.floor((d.age_s%86400)/3600);";
    html += "document.getElementById('age').textContent=d.age_s>0?(ageD+'d '+ageH+'h'):'unknown';";
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

    // Deliberate rename: edit OTA_HOSTNAME/passwords in secrets.h, flash,
    // then hit this once to drop the sticky NVS copies and adopt the new
    // values on reboot. See identity.h for why this needs to be explicit.
    server.on("/api/reset-identity", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!requireAuth(request)) return;
        request->send(200, "text/plain", "resetting, rebooting now");
        identity::resetAndReboot();
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

    ws.onEvent([](AsyncWebSocket*, AsyncWebSocketClient*, AwsEventType, void*, uint8_t*, size_t) {
        // No client->server messages expected; presence handling not needed.
    });
    server.addHandler(&ws);

    server.begin();
}

void handle(uint32_t nowMs) {
    if (WiFi.status() != WL_CONNECTED) return;

    ws.cleanupClients();

    if (nowMs - lastBroadcastMs < BROADCAST_INTERVAL_MS) return;
    lastBroadcastMs = nowMs;

    if (ws.count() > 0) {
        ws.textAll(statusJson());
    }
}

}
