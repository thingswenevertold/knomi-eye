#pragma once

// Dev convenience: WiFi + ArduinoOTA so firmware can be pushed without
// re-entering BOOT mode by hand every time. Not meant for the shipped
// tamagotchi experience — just the dev loop.
namespace ota {

void begin();
void handle();
bool isConnected();

// Brings ArduinoOTA up. begin() calls this when it associates; call it again
// if WiFi only arrives later, e.g. after provisioning over Bluetooth.
void startServices();

}
