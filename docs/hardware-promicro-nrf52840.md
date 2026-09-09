# Hardware Reference – Pro Micro nRF52840

Board target: `promicro_nrf52840/nrf52840/uf2`
(also sold as *SuperMini nRF52840*; nice!nano compatible)

| Property | Value |
|----------|-------|
| MCU | nRF52840, Cortex-M4F, single core @ 64 MHz |
| RAM | 256 kB (no PSRAM) |
| Flash | 1 MB internal — 792 kB application slot under the UF2 bootloader |
| Radio | Bluetooth LE 5, SoftDevice Controller |
| Bootloader | Adafruit UF2 (factory) |
| Console | USB CDC ACM |

---

## Pin assignment

All pins are defined in
[`boards/promicro_nrf52840_common.dtsi`](../boards/promicro_nrf52840_common.dtsi),
never in the C sources.

| Function | nRF52840 pin | Peripheral | Original (M5Stack Basic v2.7) |
|----------|--------------|------------|-------------------------------|
| Display SCK | P0.20 | `spi3` (SPIM3) | GPIO18 |
| Display MOSI | P0.17 | `spi3` | GPIO23 |
| Display CS | P0.22 | GPIO | GPIO14 |
| Display DC | P0.24 | GPIO | GPIO27 |
| Display RESET | P1.00 | GPIO | GPIO33 |
| Display backlight | P0.11 | `pwm1` channel 0 | GPIO32 (LEDC) |
| GPS TX (→ module RX) | P0.09 | `uart0` | GPIO17 |
| GPS RX (← module TX) | P0.10 | `uart0` | GPIO16 |
| Button A (Shutter) | P0.02 | GPIO, pull-up | GPIO39 |
| Button B (Next) | P0.29 | GPIO, pull-up | GPIO38 |
| Button C (Options) | P0.31 | GPIO, pull-up | GPIO37 |
| Status LED | P0.15 | `pwm0` channel 0 | GPIO2 (WS2812) |
| Console / logs | — | USB CDC ACM | UART0 |

Unused and free for future work: **P0.06, P0.08, P1.04, P1.06, P1.11, P1.13, P1.15**.

`i2c0`, `i2c1` and `spi2` are disabled in the overlay so their default pins
(P1.00, P0.11, P1.04, P1.06, P1.01, P1.02, P1.07) are available.

### Why these pins

- **SPIM3** is the only SPI instance on the nRF52840 that reaches 32 MHz, which
  the display needs; the other instances top out at 8 MHz.
- **MISO is not wired.** The panel is write-only, so the `zephyr,mipi-dbi-spi`
  node is marked `write-only` and one pin is saved.
- **P0.09 / P0.10** are the NFC pins. The board's devicetree sets
  `nfct-pins-as-gpios`, so they are ordinary GPIOs and are the natural place for
  the GPS UART, which the board already routes `uart0` to.
- **P0.15** carries the on-board LED and is the only LED on the board, so
  `light_logic` drives brightness instead of colour.

---

## Wiring

### Display (ST7789V, 240×320 panel used in landscape)

```
Display        nRF52840
---------      --------
VCC       ---- 3V3
GND       ---- GND
SCL/SCK   ---- P0.20
SDA/MOSI  ---- P0.17
CS        ---- P0.22
DC        ---- P0.24
RES       ---- P1.00
BLK       ---- P0.11
```

The panel is driven at 320×240 with `mdac = 0x60` (row/column exchange plus
column mirror), which reproduces the original M5Stack Basic layout so the whole
UI could be reused unchanged.

### GPS (ATGM336H or compatible, 9600 baud)

```
GPS module     nRF52840
----------     --------
VCC       ---- 3V3
GND       ---- GND
TX        ---- P0.10   (module transmits, nRF receives)
RX        ---- P0.09   (nRF transmits, module receives)
```

### Buttons

```
        P0.02 ----[ Button A ]---- GND
        P0.29 ----[ Button B ]---- GND
        P0.31 ----[ Button C ]---- GND
```

Internal pull-ups are enabled, so no external resistors are required.

---

## Resource budget

Measured on the completed port:

| Resource | Used | Available |
|----------|------|-----------|
| Flash | 709 kB | 792 kB (application slot) |
| RAM | 158 kB | 256 kB |

The largest RAM consumers are the LVGL rendering buffers (two partial buffers
of 17% of the display, ~52 kB together, matching the original's 320×40 double
buffer) and the Bluetooth host with four connection slots.

Adding MCUboot for OTA would halve the application slot and does not currently
fit; flashing goes through the factory UF2 bootloader instead.
