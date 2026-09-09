/*
 * ESP-IDF NVS flash-init compatibility shim.
 *
 * See nvs.h and nvs_flash.c. Backed by Zephyr's NVS on the board's
 * `storage_partition`.
 */

#ifndef __NVS_FLASH_H__
#define __NVS_FLASH_H__

#include "esp_err.h"

/** @brief Mount the storage partition. Safe to call more than once. */
esp_err_t nvs_flash_init(void);

/** @brief Erase every entry in the storage partition. */
esp_err_t nvs_flash_erase(void);

#endif /* __NVS_FLASH_H__ */
