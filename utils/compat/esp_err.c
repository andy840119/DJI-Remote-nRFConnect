/*
 * ESP-IDF error code compatibility shim -- implementation.
 */

#include <errno.h>
#include <stdio.h>

#include "esp_err.h"

const char *esp_err_to_name(esp_err_t code)
{
	switch (code) {
	case ESP_OK:
		return "ESP_OK";
	case ESP_FAIL:
		return "ESP_FAIL";
	case ESP_ERR_NO_MEM:
		return "ESP_ERR_NO_MEM";
	case ESP_ERR_INVALID_ARG:
		return "ESP_ERR_INVALID_ARG";
	case ESP_ERR_INVALID_STATE:
		return "ESP_ERR_INVALID_STATE";
	case ESP_ERR_INVALID_SIZE:
		return "ESP_ERR_INVALID_SIZE";
	case ESP_ERR_NOT_FOUND:
		return "ESP_ERR_NOT_FOUND";
	case ESP_ERR_NOT_SUPPORTED:
		return "ESP_ERR_NOT_SUPPORTED";
	case ESP_ERR_TIMEOUT:
		return "ESP_ERR_TIMEOUT";
	case ESP_ERR_INVALID_RESPONSE:
		return "ESP_ERR_INVALID_RESPONSE";
	case ESP_ERR_INVALID_CRC:
		return "ESP_ERR_INVALID_CRC";
	case ESP_ERR_NOT_FINISHED:
		return "ESP_ERR_NOT_FINISHED";
	case ESP_ERR_NOT_ALLOWED:
		return "ESP_ERR_NOT_ALLOWED";
	case ESP_ERR_NVS_NOT_FOUND:
		return "ESP_ERR_NVS_NOT_FOUND";
	case ESP_ERR_NVS_NO_FREE_PAGES:
		return "ESP_ERR_NVS_NO_FREE_PAGES";
	case ESP_ERR_NVS_NEW_VERSION_FOUND:
		return "ESP_ERR_NVS_NEW_VERSION_FOUND";
	default:
		return "ESP_ERR_UNKNOWN";
	}
}

esp_err_t esp_err_from_errno(int err)
{
	if (err >= 0) {
		return ESP_OK;
	}

	switch (-err) {
	case ENOMEM:
		return ESP_ERR_NO_MEM;
	case EINVAL:
		return ESP_ERR_INVALID_ARG;
	case ENOTSUP:
		return ESP_ERR_NOT_SUPPORTED;
	case ENOENT:
		return ESP_ERR_NOT_FOUND;
	case ETIMEDOUT:
		return ESP_ERR_TIMEOUT;
	case EBUSY:
	case EALREADY:
	case EAGAIN:
		return ESP_ERR_INVALID_STATE;
	default:
		return ESP_FAIL;
	}
}
