#pragma once
#include <cstdint>

// Bluetooth Low Energy control surface.
//
// Exposes the same tuning JSON as the web dashboard, over the Nordic UART
// Service UUIDs. Those are a de-facto standard, so the device is usable
// straight away from any free BLE terminal app — no custom client needed —
// as well as from the Web Bluetooth page in tools/ble-remote.html.
//
// Why BLE and not classic Bluetooth serial: classic SPP coexists badly with
// WiFi on a single 2.4GHz radio, iOS cannot speak it at all, and Web
// Bluetooth only talks BLE.
//
// Protocol, both directions, one JSON object per message:
//   phone -> device   any subset of the tuning fields, e.g. {"fgR":0,"fgG":255,"fgB":120}
//   device -> phone   the full state, pushed on every change
namespace ble {

// The advertised name and the optional pairing PIN both come from
// include/secrets.h, so credentials stay confined to the modules that
// actually need them.
void begin();

// Call from loop(). Cheap: only pushes a notification when something moved.
void handle(uint32_t nowMs);

bool isConnected();

}
