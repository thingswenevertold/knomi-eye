#include "ble.h"
#include "wifiprov.h"
#include "../ui/face.h"
#include "../ui/mood.h"
#include "../ui/tuning.h"
#include "../util/minijson.h"

#if __has_include("../../include/secrets.h")
#include "../../include/secrets.h"
#endif

#ifndef BLE_NAME
#define BLE_NAME "knomi-eye"
#endif

// Optional pairing PIN. Zero (or absent) leaves the link open, which is how
// the device ships so that a mistyped PIN can never lock you out of the only
// control surface that works without a network.
#ifndef BLE_PASSKEY
#define BLE_PASSKEY 0
#endif

#include <NimBLEDevice.h>

namespace {

// Nordic UART Service. Deliberately not a custom UUID: every generic BLE
// terminal app on the store already knows these, so the device is usable
// without writing a client first.
const char* SVC_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
const char* RX_UUID  = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";  // phone -> device
const char* TX_UUID  = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";  // device -> phone

NimBLECharacteristic* txChar = nullptr;
bool connected = false;
bool statePending = true;
uint32_t lastPushMs = 0;

// Commands arrive on the NimBLE task. Joining a network blocks for seconds,
// and blocking that task drops the very connection we would answer on, so
// writes are queued here and executed from the main loop instead.
volatile bool cmdPending = false;
String cmdBody;

bool scanRequested = false;
String pendingReply;      // a one-off payload to send instead of the state

constexpr uint32_t MIN_PUSH_INTERVAL_MS = 120;

// The state is the tuning document with the WiFi block folded in, so a client
// gets everything it needs from a single notification.
String fullState() {
    String t = tuning::toJson();
    if (t.endsWith("}")) t.remove(t.length() - 1);
    t += ",\"wifi\":" + wifiprov::statusJson() + "}";
    return t;
}

void setTx(const String& payload) {
    if (txChar == nullptr) return;
    txChar->setValue((uint8_t*)payload.c_str(), payload.length());
}

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* server) override {
        connected = true;
        statePending = true;
    }
    void onDisconnect(NimBLEServer* server) override {
        connected = false;
        // Without this the device stops being discoverable after the first
        // client goes away.
        NimBLEDevice::startAdvertising();
    }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* chr) override {
        std::string v = chr->getValue();
        if (v.empty()) return;
        cmdBody = String(v.c_str());
        cmdPending = true;
    }
};

// Runs on the main loop, where blocking is safe.
void runCommand(const String& body) {
    String cmd;
    if (minijson::getString(body, "cmd", cmd)) {
        if (cmd == "scan") {
            wifiprov::startScan();
            scanRequested = true;
            return;
        }
        if (cmd == "join") {
            String ssid, pass;
            minijson::getString(body, "ssid", ssid);
            minijson::getString(body, "pass", pass);
            const bool ok = wifiprov::join(ssid, pass, 12000);
            pendingReply = String("{\"joined\":") + (ok ? "true" : "false") +
                           ",\"wifi\":" + wifiprov::statusJson() + "}";
            return;
        }
        if (cmd == "remember") {
            // Enregistre sans tenter la connexion : le reseau vise n'est pas
            // forcement a portee au moment ou on le confie a la carte.
            String ssid, pass;
            minijson::getString(body, "ssid", ssid);
            minijson::getString(body, "pass", pass);
            wifiprov::remember(ssid, pass);
            return;
        }
        if (cmd == "forget") {
            // Un ssid retire ce reseau precis ; son absence efface la liste.
            String ssid;
            minijson::getString(body, "ssid", ssid);
            wifiprov::forget(ssid);
            return;
        }
        if (cmd == "anim") {
            String name;
            minijson::getString(body, "name", name);
            face::playAnim(name.c_str());
            return;
        }
        if (cmd == "presence") {
            bool away = false;
            minijson::getBool(body, "away", away);
            mood::setPcAway(away);
            return;
        }
        if (cmd == "pet") {
            // Exactly what the physical button does, so a tap on a phone and
            // a poke on the device are the same event to the mood layer.
            face::notifyInteraction(millis());
            return;
        }
        if (cmd == "list") {
            // The catalogue a remote needs to label its buttons. Sent only on
            // request: the periodic state push has to stay inside one BLE
            // notification, and these names would not fit there.
            // Positionnel comme cote HTTP : nom vide pour une creature
            // retiree, jamais d'entree omise.
            String j = "{\"skinNames\":[";
            for (int i = 0; i < face::getSkinCount(); i++) {
                if (i) j += ",";
                j += "\"" + (face::isSkinHidden(i) ? String("")
                                                    : minijson::escape(face::getSkinName(i))) + "\"";
            }
            j += "],\"animNames\":[";
            for (int i = 0; i < face::getAnimCount(); i++) {
                if (i) j += ",";
                j += "\"" + minijson::escape(face::getAnimName(i)) + "\"";
            }
            j += "]}";
            pendingReply = j;
            return;
        }
    }

    // No command verb: it is a tuning document. Unknown keys are ignored and
    // values clamped there, so a malformed write cannot corrupt the state.
    tuning::applyJson(body);
}

}

namespace ble {

void begin() {
    NimBLEDevice::init(BLE_NAME);
    // Modest transmit power: this is a desk toy, and turning the radio down
    // eases coexistence with WiFi on the shared 2.4GHz front end.
    NimBLEDevice::setPower(ESP_PWR_LVL_P3);

#if BLE_PASSKEY != 0
    // Bond + MITM protection with a fixed display-only passkey: the phone
    // prompts for the number and the link is encrypted afterwards. This is
    // what stops a WiFi passphrase crossing the air in the clear.
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityPasskey(BLE_PASSKEY);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
#endif

    NimBLEServer* server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    NimBLEService* svc = server->createService(SVC_UUID);

    // READ as well as NOTIFY: a client that has not subscribed can still
    // fetch the state, which is what most generic terminal apps do first.
    txChar = svc->createCharacteristic(
        TX_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
#if BLE_PASSKEY != 0
        | NIMBLE_PROPERTY::READ_ENC
#endif
    );

    NimBLECharacteristic* rxChar = svc->createCharacteristic(
        RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
#if BLE_PASSKEY != 0
        | NIMBLE_PROPERTY::WRITE_ENC
#endif
    );
    rxChar->setCallbacks(new RxCallbacks());

    svc->start();
    setTx(fullState());

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(SVC_UUID);
    adv->setScanResponse(true);
    adv->start();
}

void handle(uint32_t nowMs) {
    if (cmdPending) {
        cmdPending = false;
        runCommand(cmdBody);
        statePending = true;
    }

    // A scan finishes on its own schedule; forward the list as soon as it is
    // ready rather than making the client poll.
    if (scanRequested && !wifiprov::scanBusy()) {
        scanRequested = false;
        pendingReply = wifiprov::scanResultJson();
        statePending = true;
    }

    if (!connected || txChar == nullptr) return;
    if (!statePending) return;
    if (nowMs - lastPushMs < MIN_PUSH_INTERVAL_MS) return;

    lastPushMs = nowMs;

    if (pendingReply.length() > 0) {
        setTx(pendingReply);
        pendingReply = "";
    } else {
        setTx(fullState());
    }

    // Hold the flag until somebody is actually listening. A client subscribes
    // a moment after connecting, and clearing it earlier meant the first push
    // — the one carrying the initial state — went nowhere.
    if (txChar->getSubscribedCount() > 0) {
        txChar->notify();
        statePending = false;
    }
}

bool isConnected() { return connected; }

}
