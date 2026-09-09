/*
 * ESP-IDF NVS compatibility shim -- implementation on top of Zephyr's NVS.
 *
 * (namespace, key) pairs are hashed into the 16-bit ids Zephyr's NVS uses.
 * The application stores four entries, so collisions are not a practical
 * concern; a collision would show up immediately as a wrong value read back.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

#include "nvs.h"
#include "nvs_flash.h"

LOG_MODULE_REGISTER(nvs_compat, CONFIG_DJI_REMOTE_LOG_LEVEL);

#define NVS_PARTITION		storage_partition
#define NVS_PARTITION_DEVICE	FIXED_PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET	FIXED_PARTITION_OFFSET(NVS_PARTITION)
#define NVS_PARTITION_SIZE	FIXED_PARTITION_SIZE(NVS_PARTITION)

/* Open handles carry the namespace so keys of different namespaces hash apart */
#define MAX_OPEN_HANDLES 4

static struct nvs_fs s_fs;
static bool s_mounted;

static const char *s_handle_ns[MAX_OPEN_HANDLES];

/* FNV-1a over "<namespace>/<key>", folded into a usable Zephyr NVS id.
 * 0xFFFF is reserved by Zephyr's NVS, and 0 is avoided as a sentinel. */
static uint16_t entry_id(const char *ns, const char *key)
{
	uint32_t hash = 2166136261u;

	for (const char *p = ns; p != NULL && *p != '\0'; p++) {
		hash = (hash ^ (uint8_t)*p) * 16777619u;
	}
	hash = (hash ^ (uint8_t)'/') * 16777619u;
	for (const char *p = key; p != NULL && *p != '\0'; p++) {
		hash = (hash ^ (uint8_t)*p) * 16777619u;
	}

	uint16_t id = (uint16_t)((hash ^ (hash >> 16)) & 0xFFFFu);

	if (id == 0u || id == 0xFFFFu) {
		id = 1u;
	}
	return id;
}

esp_err_t nvs_flash_init(void)
{
	struct flash_pages_info info;
	int rc;

	if (s_mounted) {
		return ESP_OK;
	}

	s_fs.flash_device = NVS_PARTITION_DEVICE;
	if (!device_is_ready(s_fs.flash_device)) {
		LOG_ERR("flash device not ready");
		return ESP_FAIL;
	}

	s_fs.offset = NVS_PARTITION_OFFSET;
	rc = flash_get_page_info_by_offs(s_fs.flash_device, s_fs.offset, &info);
	if (rc != 0) {
		LOG_ERR("cannot get flash page info: %d", rc);
		return ESP_FAIL;
	}

	s_fs.sector_size = info.size;
	s_fs.sector_count = NVS_PARTITION_SIZE / info.size;

	rc = nvs_mount(&s_fs);
	if (rc != 0) {
		LOG_ERR("nvs_mount failed: %d", rc);
		return ESP_FAIL;
	}

	s_mounted = true;
	LOG_INF("storage mounted: %u sectors of %u bytes at 0x%lx",
		s_fs.sector_count, s_fs.sector_size, (unsigned long)s_fs.offset);
	return ESP_OK;
}

esp_err_t nvs_flash_erase(void)
{
	if (!s_mounted) {
		esp_err_t err = nvs_flash_init();

		if (err != ESP_OK) {
			return err;
		}
	}

	int rc = nvs_clear(&s_fs);

	if (rc != 0) {
		LOG_ERR("nvs_clear failed: %d", rc);
		return ESP_FAIL;
	}

	s_mounted = false;
	return ESP_OK;
}

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode,
		   nvs_handle_t *out_handle)
{
	ARG_UNUSED(open_mode);

	if (namespace_name == NULL || out_handle == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	if (!s_mounted) {
		esp_err_t err = nvs_flash_init();

		if (err != ESP_OK) {
			return err;
		}
	}

	for (uint32_t i = 0; i < MAX_OPEN_HANDLES; i++) {
		if (s_handle_ns[i] == NULL) {
			s_handle_ns[i] = namespace_name;
			*out_handle = i + 1;
			return ESP_OK;
		}
	}

	LOG_ERR("out of NVS handles");
	return ESP_ERR_NO_MEM;
}

void nvs_close(nvs_handle_t handle)
{
	if (handle >= 1 && handle <= MAX_OPEN_HANDLES) {
		s_handle_ns[handle - 1] = NULL;
	}
}

/* Zephyr's NVS writes through, so there is nothing to commit */
esp_err_t nvs_commit(nvs_handle_t handle)
{
	ARG_UNUSED(handle);
	return ESP_OK;
}

static const char *handle_ns(nvs_handle_t handle)
{
	if (handle < 1 || handle > MAX_OPEN_HANDLES) {
		return NULL;
	}
	return s_handle_ns[handle - 1];
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value,
		       size_t length)
{
	const char *ns = handle_ns(handle);

	if (ns == NULL || key == NULL || value == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	ssize_t written = nvs_write(&s_fs, entry_id(ns, key), value, length);

	if (written < 0) {
		LOG_ERR("nvs_write(%s/%s) failed: %d", ns, key, (int)written);
		return ESP_FAIL;
	}
	return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value,
		       size_t *length)
{
	const char *ns = handle_ns(handle);

	if (ns == NULL || key == NULL || length == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	ssize_t read = nvs_read(&s_fs, entry_id(ns, key), out_value, *length);

	if (read == -ENOENT) {
		return ESP_ERR_NVS_NOT_FOUND;
	}
	if (read < 0) {
		LOG_ERR("nvs_read(%s/%s) failed: %d", ns, key, (int)read);
		return ESP_FAIL;
	}
	if ((size_t)read > *length) {
		/* The stored entry is larger than the caller's buffer */
		*length = (size_t)read;
		return ESP_ERR_INVALID_SIZE;
	}

	*length = (size_t)read;
	return ESP_OK;
}

esp_err_t nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value)
{
	return nvs_set_blob(handle, key, &value, sizeof(value));
}

esp_err_t nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *out_value)
{
	size_t length = sizeof(*out_value);

	return nvs_get_blob(handle, key, out_value, &length);
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
	const char *ns = handle_ns(handle);

	if (ns == NULL || key == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	int rc = nvs_delete(&s_fs, entry_id(ns, key));

	if (rc != 0) {
		LOG_ERR("nvs_delete(%s/%s) failed: %d", ns, key, rc);
		return ESP_FAIL;
	}
	return ESP_OK;
}
