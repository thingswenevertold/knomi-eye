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
    json += "\"visits\":" + String((unsigned long)state::visitCount()) + ",";
    json += "\"weather\":" + String((int)weather::current()) + ",";
    json += "\"weather_debug\":\"" + jsonEscape(weather::debugInfo()) + "\"";
    json += "}";
    return json;
}

String htmlPage() {
    String html;
    html.reserve(6144);
    html += "<!doctype html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>" + String(identity::hostname()) + "</title><style>";
    // Bold Pastel: cream base, saturated pastel cards, rounded, friendly sans-serif.
    html += "*{box-sizing:border-box}";
    html += "body{background:#fdfbf7;color:#2a2a2a;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;";
    html += "padding:20px;margin:0;max-width:480px}";
    html += "h1{font-size:20px;font-weight:700;margin:0 0 16px}";
    html += "h2{font-size:12px;font-weight:700;letter-spacing:.04em;color:#8a8478;text-transform:uppercase;margin:28px 0 10px}";
    html += "h2:first-of-type{margin-top:0}";
    html += ".mirror{background:#e0d4ff;border-radius:16px;padding:18px;display:flex;align-items:center;";
    html += "justify-content:center;gap:14px;margin-bottom:8px}";
    html += ".mirror span{font-size:26px;font-weight:700;color:#3a2478;letter-spacing:.1em}";
    html += ".stat-row{display:flex;gap:10px;margin-bottom:8px}";
    html += ".stat-card{flex:1;border-radius:12px;padding:12px;min-width:0}";
    html += ".stat-card .label{font-size:11px;font-weight:600;margin-bottom:4px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}";
    html += ".stat-card .value{font-size:19px;font-weight:700;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}";
    html += ".peach{background:#ffd8c2}.peach .label{color:#a35a2e}.peach .value{color:#7a3d18}";
    html += ".mint{background:#c9f0e0}.mint .label{color:#2a7a5e}.mint .value{color:#1a4a38}";
    html += ".lavender{background:#e0d4ff}.lavender .label{color:#6a4ab8}.lavender .value{color:#3a2478}";
    html += ".butter{background:#fff2b8}.butter .label{color:#a38a1e}.butter .value{color:#7a6a10}";
    html += ".card{background:#f5f0e8;border-radius:12px;padding:12px 14px;margin-bottom:8px;font-size:13px;color:#6a6a6a}";
    html += ".bar-track{background:#eee7d8;height:8px;border-radius:4px;overflow:hidden;margin-top:6px}";
    html += ".bar-fill{height:100%;width:0%;border-radius:4px;transition:width .2s linear}";
    html += ".skins{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:8px}";
    html += ".skin-btn{background:#f5f0e8;border:none;color:#6a6a6a;border-radius:20px;";
    html += "padding:8px 14px;font-family:inherit;font-size:12px;font-weight:600;cursor:pointer}";
    html += ".skin-btn.active{background:#ff7a59;color:#fff}";
    html += "table{border-collapse:collapse;width:100%;font-size:13px}";
    html += "td{padding:6px 0;color:#6a6a6a}";
    html += "td:first-child{color:#a8a296;width:45%}";
    html += "ul{list-style:none;padding:0;margin:0;font-size:13px}";
    html += "li{padding:6px 0;display:flex;justify-content:space-between;color:#6a6a6a}";
    html += "a{color:#ff7a59;text-decoration:none;font-weight:600}";
    html += "</style></head><body>";

    html += "<h1>" + String(identity::hostname()) + "</h1>";

    html += "<div class='mirror'><span id='m-eyes'>o o</span><span id='m-mouth'>-</span></div>";

    html += "<div class='stat-row'>";
    html += "<div class='stat-card peach'><div class='label'>ENERGY</div><div class='value' id='s-energy'>--%</div></div>";
    html += "<div class='stat-card mint'><div class='label'>XP</div><div class='value' id='s-xp'>-</div></div>";
    html += "<div class='stat-card lavender'><div class='label'>SKIN</div><div class='value' id='s-skin'>-</div></div>";
    html += "</div>";
    html += "<div class='card'><span id='s-visits'>-</span> visits &middot; age <span id='s-age'>-</span> &middot; " +
            String(face::seenCount()) + "/" + String(face::getSkinCount()) + " skins discovered</div>";

    html += "<h2>Skin</h2><div class='skins' id='skins'>";
    for (int i = 0; i < face::getSkinCount(); i++) {
        html += "<button class='skin-btn' data-i='" + String(i) + "' onclick='setSkin(" + String(i) +
                ")'>" + String(face::getSkinName(i)) + "</button>";
    }
    html += "</div>";

    html += "<h2>Usage</h2>";
    const char* bars[4][3] = {{"heap", "Heap", "peach"}, {"psram", "PSRAM", "mint"},
                              {"flash", "Flash", "lavender"}, {"signal", "Signal", "butter"}};
    const char* barColors[4] = {"#ff9d6f", "#4fcf9f", "#a67cff", "#e8c93a"};
    html += "<div class='card'>";
    for (int i = 0; i < 4; i++) {
        html += "<div style='margin-bottom:" + String(i < 3 ? 10 : 0) + "px'>";
        html += "<div style='display:flex;justify-content:space-between;font-size:12px;font-weight:600'>";
        html += "<span>" + String(bars[i][1]) + "</span><span id='" + String(bars[i][0]) + "-pct'>--%</span></div>";
        html += "<div class='bar-track'><div class='bar-fill' id='" + String(bars[i][0]) + "-bar' style='background:" +
                String(barColors[i]) + "'></div></div></div>";
    }
    html += "</div>";

    html += "<h2>System</h2><div class='card'><table>";
    html += "<tr><td>IP</td><td>" + WiFi.localIP().toString() + "</td></tr>";
    html += "<tr><td>Uptime</td><td id='uptime'>-</td></tr>";
    html += "<tr><td>Chip</td><td>" + String(ESP.getChipModel()) + " rev" + String(ESP.getChipRevision()) + "</td></tr>";
    html += "<tr><td>Flash size</td><td>" + String(ESP.getFlashChipSize() / (1024 * 1024)) + " MB</td></tr>";
    html += "</table></div>";

    html += "<h2>Filesystem</h2><div class='card'><ul>";
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
    html += "</ul></div>";

    html += "<script>";
    html += "function setBar(id,pct){document.getElementById(id+'-pct').textContent=pct+'%';";
    html += "document.getElementById(id+'-bar').style.width=pct+'%';}";
    html += "function setSkin(i){fetch('/api/skin?index='+i);}";
    html += "function markSkin(i){document.querySelectorAll('.skin-btn').forEach(function(b){";
    html += "b.classList.toggle('active', b.dataset.i==i);});}";
    html += "var skinNames=[";
    for (int i = 0; i < face::getSkinCount(); i++) {
        html += "'" + String(face::getSkinName(i)) + "'";
        if (i < face::getSkinCount() - 1) html += ",";
    }
    html += "];";
    html += "var ws=new WebSocket('ws://'+location.host+'/ws');";
    html += "ws.onmessage=function(ev){var d=JSON.parse(ev.data);";
    html += "document.getElementById('m-eyes').textContent=d.eyes;";
    html += "document.getElementById('m-mouth').textContent=d.mouth;";
    html += "setBar('heap',d.heap_pct);setBar('psram',d.psram_pct);";
    html += "setBar('flash',d.flash_pct);setBar('signal',d.rssi_pct);";
    html += "document.getElementById('s-energy').textContent=d.energy_pct+'%';";
    html += "document.getElementById('s-xp').textContent=d.xp;";
    html += "document.getElementById('s-skin').textContent=skinNames[d.skin]||'-';";
    html += "document.getElementById('s-visits').textContent=d.visits;";
    html += "document.getElementById('uptime').textContent=d.uptime_s+' s';";
    html += "var ageD=Math.floor(d.age_s/86400), ageH=Math.floor((d.age_s%86400)/3600);";
    html += "document.getElementById('s-age').textContent=d.age_s>0?(ageD+'d '+ageH+'h'):'unknown';";
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
