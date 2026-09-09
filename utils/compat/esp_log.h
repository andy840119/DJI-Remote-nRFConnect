/*
 * ESP-IDF logging compatibility shim.
 *
 * Maps the `ESP_LOGx(TAG, ...)` macros used throughout the original
 * DJI-Remote firmware onto Zephyr's logging subsystem, so that the ported
 * modules keep their original log statements verbatim.
 *
 * Every .c file that logs must still register a Zephyr log module, e.g.:
 *
 *     LOG_MODULE_REGISTER(dji_protocol_parser, CONFIG_DJI_REMOTE_LOG_LEVEL);
 *
 * The ESP-IDF `TAG` string is kept as a message prefix so that log output
 * stays comparable with the original firmware.
 */

#ifndef __ESP_LOG_H__
#define __ESP_LOG_H__

#include <zephyr/logging/log.h>

/* The tag is passed as an argument rather than concatenated, because the
 * ported files declare it both ways: `#define TAG "X"` and
 * `static const char *TAG = "X";`. */
#define ESP_LOGE(tag, fmt, ...) LOG_ERR("[%s] " fmt, tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) LOG_WRN("[%s] " fmt, tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) LOG_INF("[%s] " fmt, tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) LOG_DBG("[%s] " fmt, tag, ##__VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...) LOG_DBG("[%s] " fmt, tag, ##__VA_ARGS__)

/* Log level argument of ESP_LOG_BUFFER_HEX_LEVEL -- kept for source
 * compatibility; Zephyr picks the level from the macro that is used. */
#define ESP_LOG_INFO 3

#define ESP_LOG_BUFFER_HEX(tag, buffer, len)                LOG_HEXDUMP_INF(buffer, len, tag)
#define ESP_LOG_BUFFER_HEX_LEVEL(tag, buffer, len, level)   LOG_HEXDUMP_INF(buffer, len, tag)

#endif /* __ESP_LOG_H__ */
