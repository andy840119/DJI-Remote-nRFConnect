/*
 * ESP-IDF error code compatibility shim.
 *
 * The original DJI-Remote firmware returns `esp_err_t` from almost every
 * function.  Keeping the type (and the numeric values) lets the ported code
 * stay line-for-line comparable with the ESP-IDF version instead of being
 * rewritten to Zephyr's negative-errno convention.
 *
 * New code should still prefer plain Zephyr APIs; this header exists only for
 * the modules that were carried over from the original project.
 */

#ifndef __ESP_ERR_H__
#define __ESP_ERR_H__

#include <stdint.h>

#include <zephyr/logging/log.h>

typedef int32_t esp_err_t;

#define ESP_OK                      0
#define ESP_FAIL                    -1

#define ESP_ERR_NO_MEM              0x101
#define ESP_ERR_INVALID_ARG         0x102
#define ESP_ERR_INVALID_STATE       0x103
#define ESP_ERR_INVALID_SIZE        0x104
#define ESP_ERR_NOT_FOUND           0x105
#define ESP_ERR_NOT_SUPPORTED       0x106
#define ESP_ERR_TIMEOUT             0x107
#define ESP_ERR_INVALID_RESPONSE    0x108
#define ESP_ERR_INVALID_CRC         0x109
#define ESP_ERR_NOT_FINISHED        0x10C
#define ESP_ERR_NOT_ALLOWED         0x10D

/* NVS subset -- backed by the Zephyr settings subsystem in this port */
#define ESP_ERR_NVS_BASE            0x1100
#define ESP_ERR_NVS_NOT_FOUND       (ESP_ERR_NVS_BASE + 0x0a)
#define ESP_ERR_NVS_NO_FREE_PAGES   (ESP_ERR_NVS_BASE + 0x0d)
#define ESP_ERR_NVS_NEW_VERSION_FOUND (ESP_ERR_NVS_BASE + 0x10)

/* Human readable name for an esp_err_t, used in log messages. */
const char *esp_err_to_name(esp_err_t code);

/* Translate a Zephyr negative errno into the closest esp_err_t. */
esp_err_t esp_err_from_errno(int err);

#define ESP_ERROR_CHECK(x)                                                     \
	do {                                                                   \
		esp_err_t esp_error_check_rc_ = (x);                           \
		if (esp_error_check_rc_ != ESP_OK) {                           \
			LOG_ERR("ESP_ERROR_CHECK failed: %s (0x%x) at %s:%d",   \
				esp_err_to_name(esp_error_check_rc_),          \
				(unsigned int)esp_error_check_rc_,             \
				__FILE__, __LINE__);                           \
		}                                                              \
	} while (0)

#endif /* __ESP_ERR_H__ */
