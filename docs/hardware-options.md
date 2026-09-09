# Hardware Options

Which boards to buy, on both sides:

- the **nRF52840 side**, to run this port
- the **ESP32 side**, to run the original firmware as a reference for
  cross-checking the port

Memory budgets behind these recommendations are in
[memory-comparison.md](memory-comparison.md).

> Specifications below were correct when written. Board vendors revise
> hardware without renaming it — **check the display interface, flash and PSRAM
> on the product page before ordering**, especially the items marked ⚠️.

---

## Shared peripherals

Needed regardless of which MCU board you pick:

| Part | Notes |
|---|---|
| **ST7789V display** | The firmware supports 320×240 and 320×170 layouts. **SPI** interface — see the warning about parallel panels below |
| **ATGM336H GPS module** | UART, 9600 baud. Any NMEA module works; this is the one the original uses on the Waveshare target |
| **3 momentary push-buttons** | Wired between pin and GND; internal pull-ups are used, no resistors needed |

⚠️ **Display interface matters.** Both the original and this port drive the
panel over **SPI**. Modules with the same controller and resolution also exist
with an 8-bit **parallel (i8080)** interface — those need a different panel
driver, which is a real code change on either platform. Check for "SPI" in the
listing.

---

# nRF52840 side (running this port)

## Recommended: Pro Micro nRF52840

Also sold as *SuperMini nRF52840*; nice!nano compatible.

| | |
|---|---|
| Board target | `promicro_nrf52840/nrf52840/uf2` |
| Why | Cheapest and easiest nRF52840 to source; ships with the Adafruit UF2 bootloader so **no debug probe is needed** |
| Caveats | Few exposed pins (all allocated — see [hardware-promicro-nrf52840.md](hardware-promicro-nrf52840.md)); no SWD header, only pads |

This is the target the port is developed against and the only board with a
committed devicetree overlay.

## Alternatives

| Board | Verdict | Notes |
|---|---|---|
| **nRF52840 DK** | Best for development | On-board J-Link, plenty of free pins, buttons and LEDs already there. Costs several times a Pro Micro. Needs its own overlay |
| **Seeed XIAO nRF52840** | Fine | Same silicon, very small, UF2 bootloader, slightly different pinout. Needs its own overlay |
| **nRF52840 Dongle** | Avoid | Almost no exposed GPIO |
| **nRF5340 DK** | Upgrade path | 1 MB flash + 512 kB RAM on the application core. Only worth it if the flash budget (currently 89% used) or RAM becomes a blocker; the code carries over, same SDK |

### Changing board is cheap on this side

Because every pin lives in devicetree, moving to another nRF52840 board means
adding **one overlay file** (~50 lines) under `boards/` — no C code changes, no
new HAL. Contrast that with the ESP32 side, where each board needs a HAL
implementation in `main/`.

---

# ESP32 side (running the original as a reference)

Useful for cross-checking behaviour when something in the port looks wrong —
particularly BLE notifications, the wake broadcast and multi-camera handling
(see the verification plan, issue #6).

## Tier 1 — runs the original firmware unmodified

These are the only two targets the original project builds for, and both have
prebuilt binaries in its
[releases](https://github.com/rhoenschrat/DJI-Remote/releases). **No code
required — flash and go.**

### Waveshare ESP32-S3-LCD-1.9 — best value

| | |
|---|---|
| MCU | ESP32-S3R8 (8 MB PSRAM) |
| Display | 320×170 ST7789V, **SPI**, built in |
| Needs | 3 external buttons, ATGM336H GPS |
| Why | An officially supported target, display already on board, and it doubles as a general-purpose S3 dev board for other projects |

### M5Stack Basic v2.7 — the primary target

| | |
|---|---|
| MCU | ESP32-D0WDQ6-V3, 520 kB SRAM, 16 MB flash, no PSRAM |
| Display | 320×240, SPI, built in |
| Needs | M5Stack GPS Module v2.0 (a stacking module, not a generic GPS) |
| Why | The board the original was primarily developed on; buttons already there |
| Caveat | The most expensive option, and the GPS module is an extra purchase |

## Tier 2 — if the Tier 1 boards are unavailable

### LILYGO T-Display-S3

| | |
|---|---|
| MCU | ESP32-S3R8 (16 MB flash, 8 MB PSRAM) |
| Display | 1.9", 170×320 ST7789V — ⚠️ **8-bit parallel (i8080), not SPI** |
| Verdict | **Not a drop-in.** Widely available and good hardware, but the parallel panel means writing a new HAL |

What porting to it actually costs: the resolution matches the Waveshare
target's, so the whole `320×170` UI layout preset is reused as-is. The work is
confined to the panel bus — `esp_lcd_new_panel_io_i80()` instead of
`esp_lcd_new_panel_io_spi()` — plus pin definitions and a Kconfig board entry.
An afternoon, not a rewrite. But if the point of buying it is to have an
*unmodified* reference, that point is lost.

⚠️ Also note there is a **T-Display-S3 AMOLED** variant with a completely
different (QSPI, 536×240) panel. Do not buy that one for this purpose.

### Build your own — closest to a supported target

| Part | Suggestion |
|---|---|
| MCU board | **ESP32-S3-DevKitC-1 N16R8** (16 MB flash, 8 MB PSRAM) |
| Display | Generic 2.0"/2.4" **ST7789 SPI** module, 320×240 or 240×320 |
| GPS | ATGM336H |
| Buttons | 3 momentary |

This is closer to a drop-in than T-Display-S3: the SPI ST7789 path already
exists in the firmware, so the changes are **pin definitions plus a Kconfig
board entry** — no new panel driver. Pick the N16R8 over the N8R2; the price
difference is small and the PSRAM matters for anything LVGL-shaped later.

The same approach works with a plain **ESP32-DevKitC** if you want to mirror the
M5Stack Basic (single-core ESP32, 320×240) rather than an S3.

## Tier 3 — BLE behaviour only, no display

If all you want is to compare **BLE behaviour** — the highest-risk part of this
port — you do not need a display, buttons or GPS at all:

- Any ESP32 / ESP32-C3 / ESP32-S3 board running DJI's
  [Osmo-GPS-Controller-Demo](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo)
- Or just a phone with **nRF Connect for Mobile**, to inspect the camera's GATT
  database, notifications and advertising directly

This is the cheapest way to answer "is the camera behaving differently, or is
our firmware wrong?".

---

## What about ESP32-C3?

It fits, on paper: the firmware is ~1.0 MB (a 4 MB flash part is plenty) and
needs roughly 250 kB at runtime against the C3's 400 kB SRAM.

But **no board is supported out of the box**, so the C3 needs a HAL written for
it — the same work as any unsupported board, with the added downsides of a
single 160 MHz RISC-V core and no PSRAM. If you want an ESP32 for *other*
projects the C3 is a fine, cheap part; as a reference platform for this project
it is the worst of the options here.

---

## Summary

| Goal | Buy |
|---|---|
| Run this port | **Pro Micro nRF52840** + ST7789 SPI display + ATGM336H + 3 buttons |
| Develop/debug this port comfortably | **nRF52840 DK** (add an overlay) |
| Reference firmware, best value | **Waveshare ESP32-S3-LCD-1.9** + 3 buttons + ATGM336H |
| Reference firmware, no assembly | **M5Stack Basic v2.7** + M5Stack GPS Module v2.0 |
| Tier 1 unavailable | **ESP32-S3-DevKitC-1 N16R8** + generic ST7789 **SPI** module |
| Only comparing BLE | Anything, or a phone with nRF Connect for Mobile |
| Ran out of flash/RAM later | **nRF5340 DK** |
