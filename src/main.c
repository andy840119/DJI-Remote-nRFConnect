/*
 * DJI-Remote-nRFConnect -- temporary bring-up entry point.
 *
 * This file exists only until main/app_main.c is ported.  It brings up the
 * BLE layer and runs a pairing scan so the port can be exercised on real
 * hardware: every DJI camera in range is logged by ble.c and by the pairing
 * stubs in port_stubs.c.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ble.h"

LOG_MODULE_REGISTER(main, CONFIG_DJI_REMOTE_LOG_LEVEL);

#define SCAN_TIMEOUT_MS 30000

int main(void)
{
	LOG_INF("DJI-Remote-nRFConnect starting");

	if (ble_init() != ESP_OK) {
		LOG_ERR("ble_init failed");
		return 0;
	}

	while (1) {
		LOG_INF("Starting pairing scan for %d ms", SCAN_TIMEOUT_MS);
		ble_start_scan(SCAN_MODE_PAIRING, 0, SCAN_TIMEOUT_MS);

		k_sleep(K_MSEC(SCAN_TIMEOUT_MS + 5000));
	}

	return 0;
}
