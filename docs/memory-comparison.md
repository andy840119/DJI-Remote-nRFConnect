# Memory Comparison – ESP32 vs nRF52840

Measured RAM and ROM usage of the original
[DJI-Remote](https://github.com/rhoenschrat/DJI-Remote) firmware and of this
port, so the cost of the platform change is a number rather than a guess.

Everything here is **measured, not estimated**. Re-run the commands in
[How to re-measure](#how-to-re-measure) after any change that could move these
numbers.

| | Original (ESP32) | This port (nRF52840) |
|---|---|---|
| ROM / Flash | **1,046,637 B** (1.00 MB) | **709,012 B** (0.68 MB) |
| Static RAM | **104,300 B** | **207,492 B** |
| Flash headroom | 2 MB partition, 50% free | 792 kB slot, **11% free** |
| RAM headroom | see [the caveat](#the-two-numbers-are-not-directly-comparable) | 256 kB total, 21% free |

---

## Measurement conditions

| | Original | This port |
|---|---|---|
| Toolchain | ESP-IDF v5.5 | nRF Connect SDK v3.3.0 |
| Target | `esp32`, M5Stack Basic v2.7 config | `promicro_nrf52840/nrf52840/uf2` |
| Config | `sdkconfig.defaults.m5stack_basic_v27` | `prj.conf` |
| Commit | v1.2.0 | after "match the original's 64 KB LVGL heap" |

The original project also builds for the Waveshare ESP32-S3-LCD-1.9; that
variant is not measured here because the ESP32 build is the one this port
follows.

---

## ROM / Flash

### Original (ESP32)

```
Flash Code   .text        673,398
Flash Data   .rodata      231,376
IRAM         .text        121,283   (92.53% of 131,072)
             .vectors       1,028
------------------------------------------------
Total image             1,046,637 B
```

Note that on the ESP32 part of the code lives in **IRAM**, which is a separate
128 kB region. It is 92.5% full — that is why the project's
`sdkconfig.defaults` explicitly says *"Do NOT place LVGL code in IRAM"*.

### This port (nRF52840)

```
text                      531,804
rodata                    171,232
datas (flash copy)          2,508
------------------------------------------------
Total                     709,012 B  (69.24% of the 1000 kB report region)
```

### What this means

The port is **~32% smaller**. Zephyr is leaner than ESP-IDF, and the WiFi/PHY
and coexistence blobs that ESP-IDF links in unconditionally are simply absent.

But **the headroom goes the other way**:

| | Space for the application | Used |
|---|---|---|
| ESP32 (16 MB flash) | 2 MB factory partition | 50% |
| nRF52840 (1 MB flash) | 792 kB under the UF2 bootloader | **89%** |

That 83 kB of free flash is the tightest resource in the project. Adding
MCUboot for OTA would halve the slot and does not fit — see
[porting-notes.md](porting-notes.md).

---

## RAM

### Original (ESP32) — static only

```
DRAM  .bss          83,752
      .data         20,548
------------------------------------
      total        104,300 B   (83.72% of the 124,580 B static DRAM region)
```

### This port (nRF52840)

```
bss                151,657     LVGL heap, BT host, thread stacks, app statics
noinit              52,164     LVGL rendering buffers (2 x 320x40 x 2 B)
datas                2,508
------------------------------------
      total        207,492 B   (79.15% of 262,144 B)
```

### The two numbers are not directly comparable

**ESP-IDF reports static allocation only.** These are *not* in the 104,300 B:

| Allocated at runtime on ESP32 | Size |
|---|---|
| LVGL rendering buffers (`heap_caps_malloc` by `esp_lvgl_port`) | ~51 kB |
| LVGL heap (`CONFIG_LV_MEM_SIZE_KILOBYTES=64`) | 64 kB |
| Task stacks created with `xTaskCreate` (main 8 kB, LVGL, BLE host, data 4 kB, GPS 4 kB…) | ~30 kB |
| NimBLE msys pools (12 × 256 B + 24 × 320 B) | ~11 kB |

**Zephyr's report already includes all of that**: the LVGL buffers are the
`noinit` section, the LVGL heap and the thread stacks are in `bss`.

Adding the runtime allocations back to the ESP32 figure gives roughly
**250 kB actually in use**, against **207 kB** for the port — the same order of
magnitude, with the port slightly ahead.

So the honest summary is: **the port does not use more RAM than the original.**
It only *looks* that way because the two build systems draw the line between
"static" and "heap" in different places.

---

## Where the memory goes

Largest contributors on the ESP32 build (`idf.py size-components`):

| Archive | Total | of which `.bss` |
|---|---|---|
| `liblvgl__lvgl.a` | 374,123 | 66,044 |
| `libmain.a` (the application) | 121,583 | 6,428 |
| `libesp_timer.a` | 109,148 | 24 |
| `libc.a` | 86,403 | 352 |
| `libbt.a` (NimBLE host) | 77,593 | 4,905 |
| `libbtdm_app.a` (controller) | 76,758 | 2,672 |
| `libphy.a` | 44,180 | 638 |

LVGL dominates both flash and RAM on either platform. The 66 kB of LVGL `.bss`
is the 64 kB LVGL heap — which is exactly the setting that had been missed in
this port until this comparison was made (it was 16 kB; see the fix in
[#7](https://github.com/andy840119/DJI-Remote-nRFConnect/pull/7)).

**That is the practical value of this comparison**: a parameter that silently
differed from the original would have surfaced much later, on hardware, as an
LVGL allocation failure with several screens live.

---

## Implications

- **Flash is the binding constraint on the nRF52840**, not RAM. 89% of the
  application slot is used.
- **No OTA** without either dropping the UF2 bootloader or moving to a part with
  more flash.
- **RAM has ~53 kB of headroom**, which is where any future LVGL or BLE buffer
  growth has to come from.
- **A part with more memory** (e.g. nRF5340: 1 MB flash + 512 kB RAM on the
  application core) would remove both constraints, and the port would carry
  over — see [hardware-options.md](hardware-options.md).

---

## How to re-measure

### Original firmware (ESP-IDF)

The original repository must be built with the M5Stack configuration. Build in
a **short path** on Windows — the component manager silently produces broken
`managed_components` when the full path exceeds the 250-character limit:

```bash
git clone https://github.com/rhoenschrat/DJI-Remote C:/dji-esp32
cd C:/dji-esp32
cp sdkconfig.defaults.m5stack_basic_v27 sdkconfig.defaults
rm -f sdkconfig
idf.py set-target esp32
idf.py build
idf.py size
idf.py size-components
```

### This port (nRF Connect SDK)

```bash
west build -b promicro_nrf52840/nrf52840/uf2 -p always
# section detail:
arm-zephyr-eabi-size -A build/DJI-Remote-nRFConnect/zephyr/zephyr.elf
```

When these numbers move materially, update the tables above.
