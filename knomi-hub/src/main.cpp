#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#if __has_include("../include/secrets.h")
#include "../include/secrets.h"
#else
#error "include/secrets.h missing: copy include/secrets.h.example to include/secrets.h"
#endif

// knomi-hub: a stationary status/notification node for the knomi-eye
// ecosystem. It never talks to the knomi-eye devices directly (they're
// usually on a different network) — it only reads the small public status
// snapshots and firmware version that each device publishes to the shared
// GitHub repo, and displays them on a small SSD1306 OLED.
//
// It also doubles as a tag-writing "portal" (Skylanders-style): the BOOT
// button cycles which device label is armed, and placing a blank MIFARE
// tag on the RC522 writes that label into it. Reading those tags back to
// identify a physical KNOMI at the hub is a later step — this is just the
// provisioning tool.

static const char* REPO = "thingswenevertold/knomi-eye";

// Devices to track, and to offer as RFID tag labels.
static const char* DEVICES[] = { "aconit", "zaza" };
constexpr int DEVICE_COUNT = sizeof(DEVICES) / sizeof(DEVICES[0]);

// RC522 wiring (VSPI): SCK=18, MISO=19, MOSI=23 (SPI defaults), SDA/SS=5, RST=4.
constexpr int RFID_SS_PIN = 5;
constexpr int RFID_RST_PIN = 4;
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);
MFRC522::MIFARE_Key rfidKey; // factory default 0xFF...FF

constexpr int BOOT_PIN = 0;
int selectedLabel = 0;

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

// Returns true once on a fresh BOOT-button click (debounced, fires on release).
bool bootClicked() {
    static bool stableLevel = true;
    static bool rawLevel = true;
    static uint32_t lastEdgeMs = 0;

    bool level = digitalRead(BOOT_PIN);
    uint32_t now = millis();
    if (level != rawLevel) {
        rawLevel = level;
        lastEdgeMs = now;
    }
    if (level != stableLevel && now - lastEdgeMs > 30) {
        bool wasPressed = (stableLevel == LOW);
        stableLevel = level;
        if (wasPressed && stableLevel == HIGH) return true;
    }
    return false;
}

void tryWriteTag() {
    if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

    const char* label = DEVICES[selectedLabel];
    MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);
    MFRC522::StatusCode status = MFRC522::STATUS_ERROR;

    if (piccType == MFRC522::PICC_TYPE_MIFARE_MINI ||
        piccType == MFRC522::PICC_TYPE_MIFARE_1K ||
        piccType == MFRC522::PICC_TYPE_MIFARE_4K) {
        // Classic: authenticate sector 1 (block 4) with the factory-default
        // key, then write a 16-byte block.
        byte block = 4;
        byte buffer[16] = {0};
        strncpy((char*)buffer, label, sizeof(buffer));
        status = rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &rfidKey, &(rfid.uid));
        if (status == MFRC522::STATUS_OK) {
            status = rfid.MIFARE_Write(block, buffer, 16);
        }
    } else if (piccType == MFRC522::PICC_TYPE_MIFARE_UL) {
        // Ultralight / NTAG21x (very common on cheap keyfobs): no auth, but
        // pages are 4 bytes each — write pages 4 and 5 for up to 8 chars.
        byte page4[4] = {0};
        byte page5[4] = {0};
        size_t len = strlen(label);
        memcpy(page4, label, len < 4 ? len : 4);
        if (len > 4) memcpy(page5, label + 4, (len - 4) < 4 ? (len - 4) : 4);
        status = rfid.MIFARE_Ultralight_Write(4, page4, 4);
        if (status == MFRC522::STATUS_OK) {
            status = rfid.MIFARE_Ultralight_Write(5, page5, 4);
        }
    }

    if (status == MFRC522::STATUS_OK) {
        snprintf(writeMessage, sizeof(writeMessage), "WRITTEN: %s", label);
        // No button on this board to pick the next label — auto-advance so
        // scanning tags back-to-back just works: aconit, then zaza, etc.
        selectedLabel = (selectedLabel + 1) % DEVICE_COUNT;
    } else {
        snprintf(writeMessage, sizeof(writeMessage), "FAIL (%s)", rfid.PICC_GetTypeName(piccType));
    }
    writeMessageUntilMs = millis() + 2500;

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
}

void drawWriteFooter() {
    display.drawFastHLine(0, 54, SCREEN_WIDTH, SSD1306_WHITE);
    display.setCursor(0, 56);
    if (millis() < writeMessageUntilMs) {
        display.print(writeMessage);
    } else {
        display.print("next tag: ");
        display.print(DEVICES[selectedLabel]);
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

    pinMode(BOOT_PIN, INPUT_PULLUP);
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

    if (bootClicked()) {
        selectedLabel = (selectedLabel + 1) % DEVICE_COUNT;
        redrawCurrentScreen();
    }

    tryWriteTag();
    if (millis() < writeMessageUntilMs) {
        redrawCurrentScreen(); // keep the write-confirmation footer live
    }

    if (now >= nextScreenSwitchMs) {
        nextScreenSwitchMs = now + SCREEN_SWITCH_MS;
        int totalScreens = DEVICE_COUNT + 1; // one per device + one update-summary screen
        screenIndex = (screenIndex + 1) % totalScreens;
        redrawCurrentScreen();
    }

    delay(50);
}
