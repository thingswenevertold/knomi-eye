# Field report: knomi-eye on a BIQU Panda Knomi

**Written for whoever picks this up next — human or AI assistant.** The main
README invites exactly that, so this file follows the same convention: read it
top to bottom and you'll have everything from a bring-up done on hardware the
project wasn't written for.

Date: 2026-08-19. Author: Claude, working with Samuel (`saucissefarciehumaine-prog`).
Firmware built from commit `c282c43`, unmodified except for `upload_port`.

---

## TL;DR

1. **knomi-eye runs unmodified on a BIQU Panda Knomi.** Different product from the
   KNOMI V1 the README targets, same silicon, same panel, same pinout —
   **including `BL = GPIO 2`**, which this bring-up confirms on Panda hardware for
   the first time.
2. **The board intermittently boots into ROM download mode instead of running your
   firmware.** Black screen, no WiFi, no serial — indistinguishable from a dead
   board or a failed flash. There is a five-second test that tells the two apart,
   and a reliable way out.

Finding 2 is the one that costs hours if you don't know it.

---

## 1. Panda Knomi is a drop-in target

The README targets the **BIGTREETECH KNOMI V1**. Samuel's board is a **BIQU Panda
Knomi** — a different product, sold for Bambu Lab printers rather than Voron
toolheads, with its own shell and its own closed-source stock firmware.

It runs knomi-eye with **zero code changes**.

| | KNOMI V1 (per README) | Panda Knomi (measured) |
|---|---|---|
| MCU | ESP32-D0WD-V3 | ESP32-D0WD-V3 **rev 3.1**, 40 MHz crystal |
| Module | ESP32-WROVER-E | same — 16 MB flash, 8 MB PSRAM |
| Flash chip | — | manufacturer `a1`, device `4018`, 16 MB, 3.3 V strapping |
| Panel | GC9A01 240x240 SPI | same |
| Touch / IMU / speaker | none | none |

The GPIO map in `lgfx_config.hpp` works as-is:

| Signal | GPIO | Confirmed on Panda? |
|---|---|---|
| MOSI | 23 | yes |
| SCLK | 18 | yes |
| CS | 5 | yes |
| DC | 19 | yes |
| RST | 4 | yes |
| **Backlight** | **2** | **yes — this was the open unknown** |
| BOOT | 0 | yes |

Before flashing, the pinout was derived independently from
[`drbeat/PandaKnomiDIY`](https://github.com/drbeat/PandaKnomiDIY), which runs BTT's
**official closed-source Panda Knomi firmware** on a hand-wired WROVER-E. Since
that firmware has its GPIOs compiled in, its wiring is necessarily the production
wiring. It matched `pinout_knomi_v1.h` on every pin. The backlight was the one pin
that source didn't document; the bring-up settled it.

**Suggested README change:** widen the Hardware section to "BIGTREETECH KNOMI V1
and BIQU Panda Knomi", and note that the Panda's only physical difference relevant
here is its shell and its 5–24 V MX1.25 connector.

Build footprint on this hardware, for reference:

```
RAM:   15.8%  (51 648 / 327 680 bytes)
Flash: 16.9%  (1 109 713 / 6 553 600 bytes)
```

---

## 2. The board sometimes boots into download mode instead of your firmware

### Symptoms

Screen black. Device absent from the LAN. No serial output at any baud rate. It
looks exactly like a failed flash or dead hardware — it is neither, and the flash
verifies clean if you check.

What makes this genuinely nasty is that **it is intermittent**. The same board,
same cable, same USB port, alternates between running perfectly and staying dark.
That intermittency invites you to chase power faults, brownouts and bad solder
joints, none of which are involved.

### The decisive test

Costs nothing, takes five seconds, needs no button press:

```bash
python -m esptool --port COMx --before no-reset --after no-reset flash-id
```

**If esptool connects while nobody is holding BOOT, the board is sitting in ROM
download mode instead of running your firmware.** That is the whole diagnosis, and
it cleanly separates "not executing" from "executing but broken".

On Samuel's board it connected — repeatedly, with no button pressed, reporting the
chip correctly each time.

### What is actually going on

`GPIO0` low at reset is the silicon-level instruction to enter download mode, and
`GPIO0` is wired to the CH340's DTR line (with RTS on `EN`) — the standard
auto-reset arrangement, which the README already notes behaves unreliably here.

The CH340 holds whatever levels the last program left on those lines, and keeps
holding them after that program exits. Land in a state where DTR is asserted and
the board re-enters download mode on every reset, indefinitely. That is why
`--after hard-reset` does **not** rescue it: the chip resets, `GPIO0` is still
being held low, and it goes straight back. Verified.

### What reliably gets it running again

**Physically unplug and replug the USB cable.** That forces the CH340 to
re-enumerate, its lines return to their default state, and the board boots your
firmware normally. Confirmed on Samuel's board — a replug took it from "esptool
answers with nobody touching BOOT" to a running face and a device on the LAN
within about half a minute.

A software reset is not equivalent to a replug here, and that distinction is the
practical takeaway.

### A correction, and a separate power gotcha

An earlier draft of this document claimed that **any** PC USB data port prevents
the firmware from running, and that powering from a charger was the fix. **That was
wrong.** The board demonstrably runs fine on a PC USB port most of the time — it is
running on one as this is written. Three consecutive observations of download-mode
boots were over-generalised into a rule. The correction is kept here rather than
deleted so nobody re-derives it from the same evidence.

Testing that claim turned up something unrelated but worth knowing: **on a mains
USB-C phone charger the board did not power up at all.** The backlight never lit,
not even briefly, over a generous wait. That is a power-delivery failure, not a
boot-mode one.

The likely reason is the standard USB-C trap: a device whose USB-C receptacle omits
the 5.1 kΩ CC pull-down resistors is invisible to a USB-C source, which then
supplies nothing at all — by design of the specification. A USB-A host port has no
such negotiation and just provides 5 V, which is why the PC works.

**If you need the board running away from a PC**, use the MX1.25 connector
(5–24 V, the product's intended power path) or a USB-A charger with an A-to-C
cable. A C-to-C charger cable may well give you nothing, and it will look
identical to a dead board.

Consequence for the download-mode question above: it remains **untested with a
data-free power source**, because we never got the board running on one. Still an
open question, not a claim.

### Relation to the README's open quirk

The README lists, under Known quirks:

> Serial (`Serial.print`) output is unreliable to capture from this dev setup, even
> with correct baud/reset handling — root cause not fully diagnosed (CH340 bridge
> on COM4 timing quirk, suspected).

An intermittently-in-download-mode board would produce exactly that symptom: on the
occasions it is not executing, there is no application to print anything, and no
baud rate or reset handling will conjure output. That is a plausible contributor,
**not a demonstrated cause of your specific quirk** — it was measured on Panda
hardware, and Samuel's board did also stay silent on occasions when it was running.

Worth running the one-line test next time serial goes quiet, before assuming
timing. If it connects, you have your answer for that session at least.

Independently of the cause: `diag` plus `/api/status` was the right architectural
call, and on this hardware OTA is closer to essential than convenient.

---

## 3. The CH340 bridge drops long reads

Attempting to back up the 16 MB stock flash before overwriting it:

- `read-flash 0 0x1000000` at 460 800 baud died at **10.2 %** with
  `No more data to read from the serial port`.
- That failure **crashed the stub flasher**, after which nothing could re-sync —
  every subsequent attempt returned `No serial data received` until a fresh
  BOOT + replug.
- A chunked retry (16 × 1 MB at 230 400 baud, 4 attempts each) failed on all 16
  chunks for that reason.

Writing is unaffected: flashing 1.1 MB at 230 400 baud completed and verified
first time, every time. It is specifically sustained reads that fall over.

**Don't bother dumping the stock flash.** BTT publishes complete factory images —
bootloader, partition table, firmware and the GIF image — for v1.0.2 through
v1.0.4.1 at
[`bigtreetech/PandaKnomi/Firmware`](https://github.com/bigtreetech/PandaKnomi/tree/master/Firmware).
A full factory restore is four files and needs no backup.

---

## 4. Flashing over a stock Panda Knomi

`partitions_16MB.csv` is not compatible with the stock layout, and overwrites it:

| | Stock Panda Knomi | knomi-eye |
|---|---|---|
| bootloader | `0x1000` | `0x1000` |
| partition table | `0x8000` | `0x8000` |
| app | `0x10000` | `0x10000` (`app0`, up to `0x650000`) |
| GIF image | `0x910000` | — (region falls inside `app0`/`app1`) |
| filesystem | — | `spiffs` at `0xC90000` |

The stock GIF partition is destroyed by the first flash. Restoring means writing
all four factory files back, not just the app.

Entering download mode is manual and unreliable, as the README says. Two things
help when it refuses:

- Check `DEVPKEY_Device_LastArrivalDate` on the CH340 (Windows) to confirm the
  replug even registered before blaming the button.
- `No serial data received` at **both** baud rates and **both** reset modes means
  not-in-download-mode, not a broken board.

---

## 5. Networking notes

- The device registered with the router's DNS as `knomi-eye.home`, and
  `knomi-eye.local` / `knomi-eye` resolve too. A first lookup failed only because
  the query ran seconds after boot, before registration. **But see the hostname
  collision below before trusting that name.**
- Reserve the IP with a static DHCP lease *if the board lives on one network*.
  It does not help a board you carry between sites — an mDNS hostname does.
  Find it by MAC prefix `C8:2E:18`, Espressif's OUI.
- `GET /` and `GET /api/status` both correctly return `401` without credentials.
  This is a useful probe: on this server build a route that **exists** answers
  `401` without credentials, while an unregistered path answers `500`. So
  `401` vs `500` tells you whether a firmware has a given route, with no
  password needed.

### Two boards, one hostname — cost a morning

`OTA_HOSTNAME` ends up as both the WiFi hostname and the mDNS name. Flash the
same value onto two boards and `<name>.local` resolves to whichever answers
first. There is no error, no warning, and no sign anything is wrong: the name
resolves, the server replies, everything looks healthy.

What it looked like, on a network with two boards both called `knomi-eye`:

- a serial flash reported `Hash of data verified` and `SUCCESS`
- the new firmware's routes still answered `500`, i.e. absent
- an OTA push with the freshly-flashed password answered `Authentication Failed`
- a power cycle changed nothing

Every one of those is consistent with "the flash silently failed", which is the
wrong conclusion and an expensive one. The flash was fine. The queries were
going to the other board.

**The check that settles it, in one line:** resolve the name, then compare the
MAC behind that IP against the MAC `esptool` prints when it connects over USB.

```
ping -n 1 <name>.local
arp -a | findstr c8-2e-18
```

Two `C8:2E:18` rows means two boards, and the one you are flashing is the one
whose MAC matches `esptool`'s. Note the WiFi MAC and the BT MAC differ by one
in the last byte on ESP32, so `...:24` over USB and `...:26` in a BLE scan are
the same chip.

The fix is a unique `OTA_HOSTNAME` per board, not a pinned IP: the name follows
the board onto any network, which a DHCP reservation cannot do. Pinning
`upload_port` to an IP is a stopgap for one site, worth doing while a stale
hostname is still flashed on the other board.

**Do not diagnose a device over the network until you have confirmed the thing
answering is the thing you are holding.**

---

## Confidence

**Measured directly on the hardware:** everything in sections 1, 3, 4 and 5. In
section 2: the symptoms, the decisive test, the fact that the board repeatedly sat
in download mode with no button pressed, that `--after hard-reset` does not clear
it, and that a physical replug does.

**Explanation, well-supported but not proven:** the DTR-line-state mechanism in
"What is actually going on".

**Measured, but only once:** that a mains USB-C charger failed to power the board
at all. The CC-resistor explanation for it is inference, not measurement.

**Open questions, deliberately not claimed:** whether a data-free power source
avoids the download-mode problem — untested, since we never got the board running
on one — and whether any of this causes the README's serial quirk on your board.

One earlier claim in this document was wrong. It is retained as a correction under
"A correction, and a separate power gotcha" rather than quietly deleted, so nobody
re-derives it from the same evidence.
