# Porting Notes – ESP-IDF → nRF Connect SDK

How each part of [rhoenschrat/DJI-Remote](https://github.com/rhoenschrat/DJI-Remote)
was carried over, and where the two trees intentionally differ. Useful when
diffing a file against its counterpart in the original project.

---

## Porting rules

1. **Keep the original names.** File, function and variable names are unchanged
   wherever the Zephyr coding standard allows it, so both trees can be diffed
   directly.
2. **Keep the original comments.** File headers, function docs and inline notes
   come across as written. A comment is only edited when the code it describes
   had to change for the platform — for example the NimBLE mechanics in
   `ble/ble.c`.
3. **Shim, don't rewrite.** ESP-IDF idioms the original leans on are
   re-implemented on Zephyr in `utils/compat/` rather than rewritten at every
   call site.
4. **Port faithfully first.** Parameter values (buffer sizes, MTU, stack sizes,
   refresh period) keep the original numbers even where the nRF52840 is tight.
   Reductions forced by the hardware are made in separate, single-purpose pull
   requests so they can be found and reverted on a larger part later.

---

## Compatibility shims

Everything in `utils/compat/` exists only to serve the ported files. New code
should call the Zephyr APIs directly.

| Shim | Replaces | Implemented with |
|------|----------|------------------|
| `esp_err.h` / `.c` | `esp_err_t`, `ESP_OK`, `ESP_ERR_*`, `esp_err_to_name()`, `ESP_ERROR_CHECK()` | plain integers and a lookup table |
| `esp_log.h` | `ESP_LOGE/W/I/D/V(TAG, ...)`, `ESP_LOG_BUFFER_HEX*` | `zephyr/logging` |
| `freertos_compat.h` / `.c` | semaphores, mutexes, software timers, queues, `vTaskDelay`, `xTaskGetTickCount`, `xTaskCreate` | `k_sem`, `k_msgq`, `k_work_delayable`, `k_thread` |
| `nvs.h`, `nvs_flash.h` / `nvs_flash.c` | ESP-IDF NVS (`nvs_open`/`get`/`set`/`erase_key`) | Zephyr NVS on `storage_partition` |
| `driver/uart.h` / `uart_compat.c` | ESP-IDF UART driver (`uart_driver_install`, `uart_read_bytes`, …) | interrupt-driven Zephyr UART feeding a ring buffer |
| `esp_lvgl_port.h` | `lvgl_port_lock()` / `lvgl_port_unlock()` | Zephyr LVGL's `lvgl_lock()` / `lvgl_unlock()` |
| `esp_random.h` | `esp_random()` | `sys_rand32_get()` (nRF52840 hardware RNG) |

Semantics worth knowing:

- **Ticks are milliseconds.** The original sets `CONFIG_FREERTOS_HZ=1000`, so
  `pdMS_TO_TICKS()` is the identity and `portTICK_PERIOD_MS` is 1.
- **Timer callbacks run in thread context** on the system workqueue. A raw
  `k_timer` would run them in interrupt context, which the original callbacks
  (they log and take mutexes) do not allow.
- **Priorities are inverted.** FreeRTOS counts up, Zephyr counts down;
  `xTaskCreate()` converts.
- **Task stacks are in bytes**, following ESP-IDF's deviation from vanilla
  FreeRTOS.
- **NVS keys are hashed.** ESP-IDF addresses entries by
  (namespace, key) strings, Zephyr by a 16-bit id, so the shim hashes
  `"namespace/key"` into that id.

---

## Module by module

| Module | State | Notes |
|--------|-------|-------|
| `protocol/` | unchanged | Only needed the logging macros |
| `utils/crc/` | unchanged | DJI-specific polynomials, no platform code |
| `data/` | unchanged | FreeRTOS includes swapped for the shim |
| `logic/connect_logic.c`, `command_logic.c`, `status_logic.c`, `enums_logic.c` | unchanged | Plus the HAL header swap for `M5_COLOR_*` |
| `logic/light_logic.c` | partly rewritten | WS2812-over-RMT becomes a single-colour LED on PWM |
| `gps/gps_reader.c` | unchanged | Talks to the UART shim |
| `main/ui*.c`, `icons.c`, `lvgl_icons.c`, `splash_logo.c` | unchanged | Same LVGL version (9.5.0) as the original |
| `main/app_main.c` | lightly edited | Single-board HAL macros, `int main()` instead of `void app_main()` |
| `main/*_hal.c` | rewritten | See below |
| `ble/ble.c` | rewritten | See below |
| `log_config.c` | rewritten body | Same API |

---

## BLE: NimBLE → Zephyr host

`ble/ble.h` is unchanged, so every caller in `logic/` and `data/` compiles
untouched. `ble/ble.c` is a rewrite.

| NimBLE | Zephyr |
|--------|--------|
| single `gap_event_cb()` | `BT_CONN_CB_DEFINE` + a scan callback + per-operation callbacks |
| `conn_handle` | `bt_conn_index()`; the owning `struct bt_conn *` is kept per slot |
| `ble_gattc_disc_all_svcs/chrs/dscs` chain | `bt_gatt_discover()` chain — the parameter struct must stay alive, so it lives in a per-slot context |
| CCCD write + `BLE_GAP_EVENT_NOTIFY_RX` | `bt_gatt_subscribe()`, which does both |
| `ble_gap_disc()` timeout argument | `k_work_delayable` (Zephyr's scan has no timeout argument) |
| `esp_timer` advertising auto-stop | `k_work_delayable` |
| hand-assembled advertising bytes | `BT_DATA_MANUFACTURER_DATA`, still a connectable ADV_IND as DJI requires |
| `ble_svc_gap_init()` / `ble_svc_gatt_init()` | the Zephyr host registers GAP/GATT itself |
| `ble_att_set_preferred_mtu(500)` | `CONFIG_BT_L2CAP_TX_MTU=500` |
| security disabled via `ble_hs_cfg.sm_*` | `CONFIG_BT_SMP=n` |

`CONFIG_BT_MAX_CONN=4`: three cameras plus one spare link, because the wake
broadcast advertises connectably and needs a free connection object.

---

## Display and UI

The ESP-IDF HALs drove the SPI bus and the ST7789 panel by hand through
`esp_lcd`, and `esp_lvgl_port` owned the LVGL task. On Zephyr both are
declarative:

- the panel is a devicetree node handled by the in-tree `st7789v` driver behind
  `zephyr,mipi-dbi-spi`
- the LVGL module binds itself to the `zephyr,display` chosen node, allocates
  the rendering buffers from Kconfig, and runs `lv_timer_handler()` on its own
  workqueue

so `main/promicro_nrf52840_hal.c` only unblanks the display, drives the
backlight PWM, reads the buttons, and returns the LVGL display handle.

Rendering buffers match the original: it configured
`buffer_size = 320 * 40` with `double_buffer = true`, which is 17% of a 320×240
display — `CONFIG_LV_Z_VDB_SIZE=17` with `CONFIG_LV_Z_DOUBLE_VDB=y`.

**External buttons** (`UI_ENABLE_EXTERNAL_BUTTONS`) are disabled: this board has
no external button header and no free pins for one, the same choice the
original makes for the Waveshare board.

---

## Configuration

| ESP-IDF | Zephyr |
|---------|--------|
| `sdkconfig.defaults.<board>` | `prj.conf` |
| `main/Kconfig.projbuild` | `Kconfig` (same syntax) |
| Board chooser in Kconfig | the Zephyr board target |
| GPIO numbers in Kconfig/headers | devicetree overlay |
| `partitions.csv` | devicetree `fixed-partitions` (from the board) |

The GPS Kconfig options are kept for parity with the original, but on Zephyr
the port and pins come from the `gps-uart` devicetree alias — the Kconfig values
are only used in log messages.

---

## Known differences in behaviour

- **Status LED**: the original shows connection state as WS2812 colours. This
  board has a single-colour LED, so colour collapses to brightness; the blink
  patterns (searching, recording) are unchanged.
- **Console**: USB CDC ACM instead of a UART, so log output starts only once
  USB has enumerated and the first boot lines can be missing.
- **No OTA**: MCUboot would halve the 792 kB application slot and does not
  currently fit. Flashing goes through the factory UF2 bootloader.
- **`%lu` format warnings**: `uint32_t` is `unsigned long` on ESP32 and
  `unsigned int` on ARM. The formats are left as the original wrote them; both
  are 32-bit, so the output is correct.
