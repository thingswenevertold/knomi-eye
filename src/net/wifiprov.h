#pragma once
#include <Arduino.h>

// WiFi provisioning over Bluetooth.
//
// Credentials live in NVS, entered from a phone over BLE — never in
// include/secrets.h. That means joining a new network needs no recompile and
// no cable, which matters because the compiled-in networks are useless the
// moment you carry the device somewhere new.
//
// The networks in secrets.h remain as a fallback, so a device that has never
// been provisioned still comes up on a known network.
//
// SECURITY: a passphrase typed here crosses the BLE link. Set BLE_PASSKEY in
// include/secrets.h before using this in a place you do not trust — without
// it the link is unencrypted and anyone in range could read the passphrase.
namespace wifiprov {

void begin();

// True when a network has been provisioned and stored.
bool hasStored();
String storedSsid();

// Tries the stored network. Blocking, up to timeoutMs.
bool joinStored(uint32_t timeoutMs);

// Tries these credentials and, on success, stores them for next boot.
// Blocking, up to timeoutMs. On failure the previous network is left intact.
bool join(const String& ssid, const String& pass, uint32_t timeoutMs);

void forget();

// Non-blocking scan. Start it, poll scanBusy(), then read the results.
void startScan();
bool scanBusy();
String scanResultJson();   // {"nets":[{"ssid":"..","rssi":-52,"lock":true},..]}

String statusJson();       // {"wifi":{"up":bool,"ssid":"..","ip":"..","rssi":n}}

}
