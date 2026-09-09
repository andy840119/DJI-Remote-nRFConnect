# DJI-Remote-nRFConnect – Multi-Camera Remote Control for DJI Osmo Action Cameras
### Powered by a Pro Micro nRF52840 and the nRF Connect SDK (Zephyr)

![Platform](https://img.shields.io/badge/platform-nRF52840-orange) ![NCS](https://img.shields.io/badge/nRF%20Connect%20SDK-v3.3.0-blue) ![License](https://img.shields.io/badge/license-MIT-green)

This project is a **port of [rhoenschrat/DJI-Remote](https://github.com/rhoenschrat/DJI-Remote)**
from ESP32 / ESP-IDF to the **nRF52840 / nRF Connect SDK**. All of the camera
control, DJI protocol handling, GPS injection and UI behaviour comes from that
project; this repository changes the platform underneath it, not the product.

It provides a Bluetooth remote controller for managing up to
**three DJI Osmo Action cameras simultaneously**, including forwarding live GPS
data to all connected cameras.

Features (unchanged from the original):

- Start/Stop recording
- Highlight tag insertion
- Sleep/Wake control
- Snapshot while sleeping
- Mode switching (QS button emulation)
- Automatic boot-time scanning & reconnection
- Multi-camera action coordination
- Live GPS injection to all connected cameras
- LVGL-based UI

# Supported Cameras

| Camera Model | Notes |
|--------------|-------|
| DJI Action 4 |  |
| DJI Action 5 Pro |  |
| DJI Action 6 |  |
| Osmo 360 | No highlight tags |

# Hardware Requirements

- **Pro Micro nRF52840** (also sold as SuperMini nRF52840; nice!nano compatible)
- **ST7789V SPI display**, 240×320 panel driven in landscape as 320×240
- **ATGM336H** (or compatible) GPS module, 9600 baud
- **3 momentary push-buttons** (no external resistors — internal pull-ups are used)
- USB-C cable

## Wiring

| Function | nRF52840 pin |
|----------|--------------|
| Display SCK | P0.20 |
| Display MOSI | P0.17 |
| Display CS | P0.22 |
| Display DC | P0.24 |
| Display RESET | P1.00 |
| Display backlight | P0.11 |
| GPS TX (module RX) | P0.09 |
| GPS RX (module TX) | P0.10 |
| Button A (Shutter) | P0.02 |
| Button B (Next / Navigation) | P0.29 |
| Button C (Options / Highlight / Sleep-Wake) | P0.31 |
| Status LED | P0.15 (on board) |

Buttons connect between the pin and GND. Console output and logs go over
**USB CDC ACM**, so no debug UART wiring is needed.

Full pin rationale and the devicetree source:
[`docs/hardware-promicro-nrf52840.md`](docs/hardware-promicro-nrf52840.md).

# Documentation

| File | Purpose |
|------|---------|
| [`docs/getting-started.md`](docs/getting-started.md) | **Setup** – installing the nRF Connect SDK, building, flashing |
| [`docs/hardware-promicro-nrf52840.md`](docs/hardware-promicro-nrf52840.md) | **Hardware Reference** – pin assignments and wiring |
| [`docs/porting-notes.md`](docs/porting-notes.md) | **Porting Reference** – how each ESP-IDF concept maps to Zephyr |
| [`docs/memory-comparison.md`](docs/memory-comparison.md) | **Memory Comparison** – measured RAM/ROM of the original firmware vs this port |
| [`docs/hardware-options.md`](docs/hardware-options.md) | **Hardware Options** – which boards to buy, on the nRF52840 and the ESP32 side |

The original project's user manual and implementation notes still describe how
this firmware behaves:
[`docs/manual.md`](https://github.com/rhoenschrat/DJI-Remote/blob/main/docs/manual.md) and
[`docs/implementation.md`](https://github.com/rhoenschrat/DJI-Remote/blob/main/docs/implementation.md).

# Building

```bash
west build -b promicro_nrf52840/nrf52840/uf2 -p always
```

# Flashing

The board ships with Adafruit's UF2 bootloader, so no debug probe is needed:

1. Bridge RST to GND twice quickly to enter the bootloader — a mass-storage
   device appears
2. Copy `build/DJI-Remote-nRFConnect/zephyr/zephyr.uf2` onto it

See [`docs/getting-started.md`](docs/getting-started.md) for the full setup.

# Project Layout

The directory layout mirrors the original project so the two trees can be
diffed file by file:

```
ble/        BLE GATT client (rewritten on the Zephyr Bluetooth host)
data/       BLE notification dispatcher
gps/        GPS UART reader and NMEA parser
logic/      Connection, command, status and light logic
main/       LVGL UI, screens, board HAL, application entry point
protocol/   DJI protocol parser and data descriptors
utils/crc/  CRC16 / CRC32 for the DJI protocol
utils/compat/  ESP-IDF compatibility shims (esp_err, esp_log, FreeRTOS, NVS, UART, LVGL port)
boards/     Devicetree overlay and pin map for the Pro Micro nRF52840
```

Rather than rewriting every ported file around native Zephyr APIs, the ESP-IDF
idioms the original leans on (`esp_err_t`, `ESP_LOGx(TAG, ...)`, FreeRTOS
semaphores/timers/queues, NVS, the UART driver) are re-implemented as thin
shims in `utils/compat/`. That keeps the ported sources comparable with the
original line for line — see [`docs/porting-notes.md`](docs/porting-notes.md).

# Origin and Credits

This is a port of:

[**DJI-Remote**](https://github.com/rhoenschrat/DJI-Remote) by rhoenschrat

which is based on
[**M5StickCPlus2_Remote_For_DJI_Osmo**](https://github.com/theserialhobbyist/M5StickCPlus2_Remote_For_DJI_Osmo),
which is based on
[**Osmo-GPS-Controller-Demo**](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo) by DJI.

# License

MIT, same as the original project. See [`LICENSE`](LICENSE).

Third-party licenses (DJI, Zephyr, LVGL and the original project) are listed in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

# Acknowledgements

- **rhoenschrat** for [DJI-Remote](https://github.com/rhoenschrat/DJI-Remote), which this port follows closely
- The creator of [M5StickCPlus2_Remote_For_DJI_Osmo](https://github.com/theserialhobbyist/M5StickCPlus2_Remote_For_DJI_Osmo)
- DJI for the [Osmo-GPS-Controller-Demo](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo)
- Nordic Semiconductor and the Zephyr project
