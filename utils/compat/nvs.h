/*
 * ESP-IDF NVS compatibility shim.
 *
 * The original firmware stores the device ID and the camera pairing table in
 * ESP-IDF's NVS, addressed by (namespace, key) string pairs. Zephyr's own NVS
 * addresses entries by a 16-bit id instead, so this shim derives the id from
 * a hash of "namespace/key" and otherwise keeps the ESP-IDF call shape, which
 * lets ui.c keep its storage code unchanged.
 *
 * Backed by the `storage_partition` (32 kB) of the board flash map.
 *
 * Only the subset the project uses is implemented: u32 and blob entries, no
 * iteration, no statistics. `nvs_commit()` is a no-op because Zephyr's NVS
 * writes through on every set.
 */

#ifndef __NVS_H__
#define __NVS_H__

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef uint32_t nvs_handle_t;

typedef enum {
	NVS_READONLY = 0,
	NVS_READWRITE = 1,
} nvs_open_mode_t;

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode,
		   nvs_handle_t *out_handle);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_commit(nvs_handle_t handle);

esp_err_t nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value);
esp_err_t nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *out_value);

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value,
		       size_t length);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value,
		       size_t *length);

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key);

#endif /* __NVS_H__ */
