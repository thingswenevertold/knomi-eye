# knomi-eye

A desk-toy tamagotchi firmware for the BIGTREETECH KNOMI V1 board, repurposed
as a tiny round-screen creature with a personality. Built with PlatformIO +
LovyanGFX, flashed over WiFi (OTA) once the first upload is done, with a web
dashboard for live monitoring and skin-switching.

This README is written to be a complete standalone reference — read it
top to bottom and you should be able to pick the project up cold, whether
that's future-you, a friend building the same hardware, or an AI assistant
helping out.

## Hardware

**Current target: BIGTREETECH KNOMI V1**
- MCU: ESP32-D0WD-V3, dual core 240MHz
- Module: ESP32-WROVER-E (16MB flash, 8MB PSRAM)
- Screen: GC9A01, round, 240x240, SPI, ~32mm diameter
- No touch, no IMU, no speaker
- Only input: the BOOT button (GPIO0) + WiFi
- Power: USB-C or MX1.25 connector (5-24V DC), no onboard LiPo charging

**GC9A01 pinout** (confirmed from `bigtreetech/KNOMI` firmware branch,
`src/pinout_knomi_v1.h` — not documented anywhere in the BTT wiki):

| Signal | GPIO |
|---|---|
| MOSI (SDA) | 23 |
| SCLK | 18 |
| CS | 5 |
| DC | 19 |
| RST | 4 |
| Backlight | 2 |
| BOOT | 0 |

No MISO is wired — the panel is write-only over SPI (VSPI natural pins).

**Future target (not yet built, but designed for):** Waveshare
ESP32-S3-Touch-LCD-2.1 — round 480x480, touch, ESP32-S3 with PSRAM, RGB
parallel interface instead of SPI. See [Architecture](#architecture) for how
the codebase is structured so that migration only touches one file.

## Getting started

### 1. Install PlatformIO

```bash
python -m pip install --user platformio
```

Or install the **PlatformIO IDE** extension in VS Code — it picks up
`platformio.ini` automatically and gives you Build/Upload/Monitor buttons
plus a proper serial monitor.

### 2. Set up your secrets

```bash
cp include/secrets.h.example include/secrets.h
```

Edit `include/secrets.h` with:
- `WIFI_SSID_1` / `WIFI_PASSWORD_1` — your primary WiFi network
- `WIFI_SSID_2` / `WIFI_PASSWORD_2` — a fallback network (leave the SSID
  empty `""` if you only have one)
- `OTA_PASSWORD` — password required to push firmware over WiFi
- `OTA_HOSTNAME` — mDNS hostname, e.g. `knomi-eye` → reachable at
  `knomi-eye.local`
- `ADMIN_PASSWORD` — HTTP Basic Auth password for the web dashboard
  (username is always `admin`)

This file is gitignored — never commit it.

### 3. First flash (over serial, BOOT mode required)

This board's USB-serial bridge does **not** reliably auto-reset into
bootloader mode. Every time you need a serial flash:

1. Hold the **BOOT** button
2. Plug in (or replug) the USB-C cable
3. Release **BOOT**

Then:

```bash
pio run -e esp32dev -t upload
```

Adjust `upload_port` / `monitor_port` in `platformio.ini` if your board
isn't on `COM4`.

### 4. Every flash after that: OTA

Once the board has connected to WiFi once, all further development can go
over the air — no cable, no BOOT-button dance:

```bash
pio run -e esp32dev-ota -t upload
```

This targets `<OTA_HOSTNAME>.local` (edit `upload_port` in
`[env:esp32dev-ota]` in `platformio.ini` if mDNS doesn't resolve on your
network — use the device's IP instead) and authenticates with
`OTA_PASSWORD`.

## Web dashboard

Once connected to WiFi, the device serves a small retro-styled dashboard at:

```
http://<OTA_HOSTNAME>.local/
```

(HTTP Basic Auth: `admin` / your `ADMIN_PASSWORD`)

It shows:
- **Live mirror** — the current face glyphs, pushed over a WebSocket
  (`/ws`) roughly 5x/second
- **Usage bars** — heap, PSRAM, flash, and WiFi signal strength as live
  percentages
- **Skin picker** — buttons to switch the active skin remotely; the choice
  persists across reboots (stored in NVS)
- **System info** — IP, uptime, chip model, flash size
- **Filesystem** — lists whatever is on the LittleFS partition, with
  download links

Useful raw endpoints:
- `GET /api/status` — JSON status snapshot (also what the WebSocket pushes)
- `GET /api/skin?index=N` — switch to skin `N`
- `GET /fs?path=/foo` — download a LittleFS file

## Physical controls

The BOOT button is the only physical input:
- **Short click** — cycles through status screens (WiFi SSID, IP, RSSI),
  each shown for 10s before returning to the face automatically
- **Long press** (>1.2s) — forces a random "special" animation immediately,
  regardless of the idle random-trigger timer (a free easter egg / demo
  trigger)

## Architecture

```
src/
  main.cpp              — setup()/loop() orchestration
  ota.cpp                — WiFi connect (2 known networks, fallback) + ArduinoOTA
  diag.cpp                — tiny cross-module debug state (button event, screen),
                             surfaced in /api/status since serial capture is
                             unreliable on this board (see Known quirks)
  display/
    lgfx_config.hpp       — the ONLY file that should change when porting to a
                             different panel/bus (e.g. the future Waveshare RGB
                             target). Defines the LovyanGFX device + pin mapping.
    display.h / .cpp      — panel-agnostic drawing API. UI code never touches
                             LovyanGFX or GPIO pins directly — only this API.
                             Coordinates are normalized [0,1] floats relative to
                             the screen's shorter side, so geometry survives the
                             240x240 -> 480x480 resolution jump untouched.
  ui/
    face.h / .cpp          — the face state machine: blink timing, random
                             "special" animations (wink/dance/wobble/surprise),
                             skin application, and dispatch to custom layouts
    skins.h / .cpp          — the skin table (palette + idle glyphs + layout)
    status.h / .cpp         — BOOT-click status screens (WiFi/IP/RSSI)
  input/
    button.h / .cpp         — BOOT button debounce + short-click/long-press
  web/
    admin_server.h / .cpp   — the web dashboard (ESPAsyncWebServer + LittleFS
                             + WebSocket)
  assets/
    cat.h/.cpp, google.h/.cpp — generated RGB565 image arrays (see below),
                             do not hand-edit
assets/
  cat.png, google.png     — source images
  convert.py               — Pillow/numpy script that generates src/assets/*
include/
  secrets.h.example        — template for your local secrets.h (gitignored)
partitions_16MB.csv        — custom partition table sized for the 16MB flash
platformio.ini              — two envs: esp32dev (serial) and esp32dev-ota (OTA)
```

### The skin system

Most skins are just a palette + a pair of idle text glyphs, applied through
one shared draw template (ring + centered eyes/mouth). A skin can instead
set a custom `layout` in `skins.cpp` to fully replace the draw path:

- `LAYOUT_DEFAULT` — the common ring + glyphs template
- `LAYOUT_CORALINE` — dark vignette, big sewn-button eyes (filled circle +
  rim + glossy highlight + 4 small stitch-hole dots, no ring), dashed
  stitched mouth
- `LAYOUT_GLITCH` — normally quiet code-glyph face; randomly (checked every
  400ms, ~25% chance) bursts into one of four payloads for a short window:
  chromatic-aberration ASCII glitch text + torn bars, TV-static block
  noise, or one of two embedded real photos (`assets/cat.png`,
  `assets/google.png`)

**Special animations** (blink, wink, dance, wobble, surprise) always use
the shared generic template regardless of the active skin's layout, so the
long-press easter egg stays meaningful on every skin — only the idle state
differs per skin.

**To add a new skin:** add one row to the `SKINS[]` array in
`src/ui/skins.cpp` (palette + idle glyphs). No firmware logic needed unless
you also want a custom `LAYOUT_*` composition, in which case add a new enum
value in `skins.h` and a `draw*Face()` function in `face.cpp`, following the
`drawCoralineFace` / `drawGlitchFace` pattern.

**To add a real image to a skin:** drop the source photo in `assets/`, run
`python assets/convert.py` (needs `pip install pillow numpy`), and it
generates a `src/assets/<name>.h/.cpp` pair with a `const uint16_t
<NAME>_IMG[]` RGB565 array (160x160 by default) compiled straight into the
firmware — no filesystem/LittleFS involved. Render it with
`display::pushImageCenteredNorm(NAME_IMG, 160, sizeNorm)`.

## Known quirks

- **BOOT-mode entry is manual, every time.** This board's auto-reset via
  RTS/DTR toggling is unreliable for entering the bootloader (though
  `esptool`'s post-flash *reset back to normal run* does work reliably).
  Always hold BOOT, replug USB-C, release BOOT before a serial flash.
- **Serial (`Serial.print`) output is unreliable to capture** from this dev
  setup, even with correct baud/reset handling — root cause not fully
  diagnosed (CH340 bridge on COM4 timing quirk, suspected). Prefer the web
  dashboard's `/api/status` and the `diag` module for runtime debugging
  over serial prints.
- **`spi_3wire` must be `false`** in `lgfx_config.hpp`. It's meant for
  panels with a single bidirectional data line, not for "MISO simply
  unused" — setting it `true` breaks write timing and gives a black screen.
  (This was the root cause of the very first bring-up failure.)

## Migrating to the future Waveshare ESP32-S3-Touch-LCD-2.1 target

The display layer is deliberately isolated for this:

1. Write a new LGFX device class in a new file (or replace
   `lgfx_config.hpp`) wired to `lgfx::Panel_RGB` / `lgfx::Bus_RGB` instead
   of `Panel_GC9A01` / `Bus_SPI`, with the S3 board's actual RGB-parallel
   pinout.
2. Update `display::begin()` in `display.cpp` to instantiate it.
3. Nothing else changes — all UI code (`face.cpp`, `status.cpp`) only calls
   the normalized `display::*Norm()` API, so 240x240 vector geometry scales
   automatically to 480x480.
4. Image assets (`assets/convert.py`) will need regenerating at a larger
   `SIZE` for the higher-resolution screen, and touch input will need a new
   `input/` module alongside (not replacing) `button.cpp`.

## Status

Actively being iterated on. Current feature set: animated ASCII face with
17 switchable skins, BOOT-button status screens + easter-egg trigger, OTA
firmware updates, and a live web dashboard.

Ideas discussed but not yet built: WiFiManager captive-portal WiFi setup
(so a friend building the same hardware doesn't need to touch
`secrets.h`), NTP-based time-of-day moods, and a lightweight mDNS-based
"do the two devices see each other" discovery step (an ESP-NOW exchange
protocol was explicitly ruled out as too heavy for now).
