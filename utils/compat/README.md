# ESP-IDF compatibility shims

The original [DJI-Remote](https://github.com/rhoenschrat/DJI-Remote) firmware is
written against ESP-IDF. To keep the ported sources line-for-line comparable
with the original (see issue #1), a handful of ESP-IDF idioms are re-implemented
on top of Zephyr instead of being rewritten:

| Shim | Replaces | Backed by |
|------|----------|-----------|
| `esp_err.h` / `esp_err.c` | `esp_err_t`, `ESP_OK`, `ESP_ERR_*`, `esp_err_to_name()` | plain integers + a lookup table |
| `esp_log.h` | `ESP_LOGE/W/I/D/V(TAG, ...)` | `zephyr/logging/log.h` |

These shims are intentionally minimal. New code written for this project should
use the native Zephyr APIs directly.
