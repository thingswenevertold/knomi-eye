#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>

#if __has_include("../include/secrets.h")
#include "../include/secrets.h"
#else
#error "include/secrets.h missing: copy include/secrets.h.example to include/secrets.h"
#endif

// knomi-hub: a stationary status/notification node for the knomi-eye
// ecosystem. It never talks to the knomi-eye devices directly (they're
// usually on a different network) — it only reads/writes small files in
// the shared GitHub repo as a relay.
//
// It's also the Skylanders-style "portal": presenting one of the RFID tags
// (provisioned earlier — see git history for the write-mode version of
// this file) identifies which KNOMI it belongs to and pushes a
// commands/<device>.json to the repo with an XP/energy bonus. Each knomi-eye
// device polls its own commands file (commandpoll.cpp) and applies it once.

static const char* REPO = "thingswenevertold/knomi-eye";

// Devices to track, and to recognize from scanned tags.
static const char* DEVICES[] = { "aconit", "zaza" };
constexpr int DEVICE_COUNT = sizeof(DEVICES) / sizeof(DEVICES[0]);

constexpr int SCAN_XP_BONUS = 5;
constexpr int SCAN_ENERGY_BONUS = 15;
constexpr uint32_t SCAN_COOLDOWN_MS = 60000; // ignore repeat scans of the same tag within 60s

// RC522 wiring (VSPI): SCK=18, MISO=19, MOSI=23 (SPI defaults), SDA/SS=5, RST=4.
constexpr int RFID_SS_PIN = 5;
constexpr int RFID_RST_PIN = 4;
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);
MFRC522::MIFARE_Key rfidKey; // factory default 0xFF...FF

Preferences prefs;
uint32_t lastScanMs[DEVICE_COUNT] = {0};

char writeMessage[40] = "";
uint32_t writeMessageUntilMs = 0;

constexpr int SCREEN_WIDTH = 128;
constexpr int SCREEN_HEIGHT = 64;
constexpr int OLED_ADDR = 0x3C;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

struct DeviceStatus {
    bool valid = false;
    String firmware;
    String skin;
    int energyPct = 0;
    unsigned long xp = 0;
    unsigned long ageS = 0;
    int weather = 0;
    bool updateAvailable = false;
};

DeviceStatus statuses[DEVICE_COUNT];
String latestFirmwareVersion = "";

void drawWriteFooter(); // defined further down, used by the draw*Screen functions below

uint32_t nextFetchMs = 0;
constexpr uint32_t FETCH_INTERVAL_MS = 120000; // 2 min

int screenIndex = 0;
uint32_t nextScreenSwitchMs = 0;
constexpr uint32_t SCREEN_SWITCH_MS = 4000;

bool fetchText(const String& url, String& out) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(6000);
    if (!http.begin(client, url)) return false;
    int code = http.GET();
    bool ok = (code == 200);
    if (ok) out = http.getString();
    http.end();
    return ok;
}

bool extractString(const String& body, const char* key, String& out) {
    int idx = body.indexOf(key);
    if (idx < 0) return false;
    idx += strlen(key);
    int start = body.indexOf('"', idx) + 1;
    int end = body.indexOf('"', start);
    if (start <= 0 || end < 0) return false;
    out = body.substring(start, end);
    return true;
}

bool extractLong(const String& body, const char* key, unsigned long& out) {
    int idx = body.indexOf(key);
    if (idx < 0) return false;
    idx += strlen(key);
    while (idx < (int)body.length() && body[idx] == ' ') idx++;
    int start = idx;
    while (idx < (int)body.length() && isDigit(body[idx])) idx++;
    if (idx == start) return false;
    out = body.substring(start, idx).toInt();
    return true;
}

void refreshAll() {
    fetchText("https://raw.githubusercontent.com/" + String(REPO) + "/master/firmware/version.txt",
              latestFirmwareVersion);
    latestFirmwareVersion.trim();

    for (int i = 0; i < DEVICE_COUNT; i++) {
        String url = "https://raw.githubusercontent.com/" + String(REPO) + "/master/status/" +
                     String(DEVICES[i]) + ".json";
        String body;
        if (!fetchText(url, body)) {
            statuses[i].valid = false;
            continue;
        }

        DeviceStatus s;
        s.valid = true;
        extractString(body, "\"firmware\":", s.firmware);
        extractString(body, "\"skin\":", s.skin);
        unsigned long v;
        if (extractLong(body, "\"energy_pct\":", v)) s.energyPct = (int)v;
        if (extractLong(body, "\"xp\":", v)) s.xp = v;
        if (extractLong(body, "\"age_s\":", v)) s.ageS = v;
        if (extractLong(body, "\"weather\":", v)) s.weather = (int)v;
        s.updateAvailable = latestFirmwareVersion.length() > 0 && s.firmware != latestFirmwareVersion;
        statuses[i] = s;
    }
}

void drawDeviceScreen(int i) {
    const DeviceStatus& s = statuses[i];
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(DEVICES[i]);
    display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);

    if (!s.valid) {
        display.setCursor(0, 24);
        display.println("no data");
        drawWriteFooter();
        display.display();
        return;
    }

    display.setCursor(0, 14);
    display.print("skin:");
    display.println(s.skin);

    display.setCursor(0, 24);
    display.print("energy:");
    display.print(s.energyPct);
    display.print("% xp:");
    display.println(s.xp);

    display.setCursor(0, 34);
    unsigned long days = s.ageS / 86400;
    unsigned long hours = (s.ageS % 86400) / 3600;
    display.print("age: ");
    display.print(days);
    display.print("d ");
    display.print(hours);
    display.println("h");

    drawWriteFooter();
    display.display();
}

void drawUpdateScreen() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("FIRMWARE");
    display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);

    display.setCursor(0, 14);
    display.print("latest: ");
    display.println(latestFirmwareVersion.length() ? latestFirmwareVersion : "?");

    int y = 24;
    for (int i = 0; i < DEVICE_COUNT && y < 50; i++) {
        display.setCursor(0, y);
        display.print(DEVICES[i]);
        display.print(": ");
        if (!statuses[i].valid) {
            display.println("?");
        } else if (statuses[i].updateAvailable) {
            display.println("UPDATE");
        } else {
            display.println("ok");
        }
        y += 10;
    }

    drawWriteFooter();
    display.display();
}

const char* B64_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String base64Encode(const String& in) {
    String out;
    out.reserve((in.length() + 2) / 3 * 4);
    size_t i = 0;
    while (i + 2 < in.length()) {
        uint32_t n = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8) | (uint8_t)in[i + 2];
        out += B64_CHARS[(n >> 18) & 0x3F];
        out += B64_CHARS[(n >> 12) & 0x3F];
        out += B64_CHARS[(n >> 6) & 0x3F];
        out += B64_CHARS[n & 0x3F];
        i += 3;
    }
    size_t rem = in.length() - i;
    if (rem == 1) {
        uint32_t n = (uint8_t)in[i] << 16;
        out += B64_CHARS[(n >> 18) & 0x3F];
        out += B64_CHARS[(n >> 12) & 0x3F];
        out += "==";
    } else if (rem == 2) {
        uint32_t n = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8);
        out += B64_CHARS[(n >> 18) & 0x3F];
        out += B64_CHARS[(n >> 12) & 0x3F];
        out += B64_CHARS[(n >> 6) & 0x3F];
        out += "=";
    }
    return out;
}

bool githubRequest(const String& method, const String& url, const String& body, String& responseOut) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(8000);
    if (!http.begin(client, url)) return false;
    http.addHeader("Authorization", String("Bearer ") + GITHUB_WRITE_TOKEN);
    http.addHeader("Accept", "application/vnd.github+json");
    http.addHeader("User-Agent", "knomi-hub");
    if (body.length() > 0) http.addHeader("Content-Type", "application/json");
    int code = http.sendRequest(method.c_str(), (uint8_t*)body.c_str(), body.length());
    responseOut = http.getString();
    http.end();
    return code >= 200 && code < 300;
}

// Pushes commands/<device>.json = {"type":"tag_scan","seq":N,"xp":X,"energy":E}.
// seq is a small per-device counter persisted in NVS, so the KNOMI can tell
// a genuinely new scan apart from re-reading the same command.
bool pushScanCommand(const char* device) {
    String seqKey = String("seq_") + device;
    uint32_t seq = prefs.getUInt(seqKey.c_str(), 0) + 1;

    String path = "commands/" + String(device) + ".json";
    String apiUrl = "https://api.github.com/repos/" + String(REPO) + "/contents/" + path;

    String getResp;
    String sha;
    if (githubRequest("GET", apiUrl + "?ref=master", "", getResp)) {
        int idx = getResp.indexOf("\"sha\":");
        if (idx >= 0) {
            int start = getResp.indexOf('"', idx + 6) + 1;
            int end = getResp.indexOf('"', start);
            if (start > 0 && end > start) sha = getResp.substring(start, end);
        }
    }

    String content = "{\"type\":\"tag_scan\",\"seq\":" + String(seq) +
                      ",\"xp\":" + String(SCAN_XP_BONUS) +
                      ",\"energy\":" + String(SCAN_ENERGY_BONUS) + "}";
    String putBody = "{\"message\":\"scan: " + String(device) + "\",\"content\":\"" +
                      base64Encode(content) + "\",\"branch\":\"master\"";
    if (sha.length() > 0) putBody += ",\"sha\":\"" + sha + "\"";
    putBody += "}";

    String putResp;
    bool ok = githubRequest("PUT", apiUrl, putBody, putResp);
    if (ok) prefs.putUInt(seqKey.c_str(), seq);
    return ok;
}

void tryReadTag() {
    if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

    MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);
    String label;

    if (piccType == MFRC522::PICC_TYPE_MIFARE_MINI ||
        piccType == MFRC522::PICC_TYPE_MIFARE_1K ||
        piccType == MFRC522::PICC_TYPE_MIFARE_4K) {
        byte block = 4;
        byte buffer[18];
        byte size = sizeof(buffer);
        MFRC522::StatusCode status = rfid.PCD_Authenticate(
            MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &rfidKey, &(rfid.uid));
        if (status == MFRC522::STATUS_OK) {
            status = rfid.MIFARE_Read(block, buffer, &size);
        }
        if (status == MFRC522::STATUS_OK) {
            char buf[17] = {0};
            memcpy(buf, buffer, 16);
            label = String(buf);
        }
    } else if (piccType == MFRC522::PICC_TYPE_MIFARE_UL) {
        byte buffer[18];
        byte size = sizeof(buffer);
        if (rfid.MIFARE_Read(4, buffer, &size) == MFRC522::STATUS_OK) {
            char buf[9] = {0};
            memcpy(buf, buffer, 8);
            label = String(buf);
        }
    }

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();

    int deviceIndex = -1;
    for (int i = 0; i < DEVICE_COUNT; i++) {
        if (label == DEVICES[i]) { deviceIndex = i; break; }
    }

    if (deviceIndex < 0) {
        snprintf(writeMessage, sizeof(writeMessage), "unknown tag");
        writeMessageUntilMs = millis() + 2000;
        return;
    }

    uint32_t now = millis();
    if (now - lastScanMs[deviceIndex] < SCAN_COOLDOWN_MS) {
        snprintf(writeMessage, sizeof(writeMessage), "%s: cooldown", DEVICES[deviceIndex]);
        writeMessageUntilMs = millis() + 1500;
        return;
    }
    lastScanMs[deviceIndex] = now;

    bool ok = pushScanCommand(DEVICES[deviceIndex]);
    if (ok) {
        snprintf(writeMessage, sizeof(writeMessage), "%s: +%d xp sent", DEVICES[deviceIndex], SCAN_XP_BONUS);
    } else {
        snprintf(writeMessage, sizeof(writeMessage), "%s: send failed", DEVICES[deviceIndex]);
    }
    writeMessageUntilMs = millis() + 2500;
}

void drawWriteFooter() {
    display.drawFastHLine(0, 54, SCREEN_WIDTH, SSD1306_WHITE);
    display.setCursor(0, 56);
    if (millis() < writeMessageUntilMs) {
        display.print(writeMessage);
    } else {
        display.print("scan a tag...");
    }
}

void drawWifiScreen() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 20);
    display.println("connecting to WiFi...");
    display.display();
}

void setup() {
    Wire.begin();
    display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
    drawWifiScreen();

    prefs.begin("hub", false);
    SPI.begin();
    rfid.PCD_Init();
    for (int i = 0; i < 6; i++) rfidKey.keyByte[i] = 0xFF;

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(200);
    }

    nextFetchMs = 0;
    nextScreenSwitchMs = 0;
}

void redrawCurrentScreen() {
    if (screenIndex < DEVICE_COUNT) {
        drawDeviceScreen(screenIndex);
    } else {
        drawUpdateScreen();
    }
}

void loop() {
    uint32_t now = millis();

    if (WiFi.status() == WL_CONNECTED && now >= nextFetchMs) {
        nextFetchMs = now + FETCH_INTERVAL_MS;
        refreshAll();
    }

    tryReadTag();
    if (millis() < writeMessageUntilMs) {
        redrawCurrentScreen(); // keep the scan-confirmation footer live
    }

    if (now >= nextScreenSwitchMs) {
        nextScreenSwitchMs = now + SCREEN_SWITCH_MS;
        int totalScreens = DEVICE_COUNT + 1; // one per device + one update-summary screen
        screenIndex = (screenIndex + 1) % totalScreens;
        redrawCurrentScreen();
    }

    delay(50);
}
