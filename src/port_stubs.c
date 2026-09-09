/*
 * Temporary port scaffolding.
 *
 * ble.c calls back into the UI and connect-logic layers, which have not been
 * ported yet.  These stubs keep the firmware linkable and log what the real
 * implementation would receive.
 *
 * Each stub is deleted as soon as its owning module lands:
 *
 *   ui_pairing_*         -> main/ui_screen_pairing.c
 *   connect_logic_*      -> logic/connect_logic.c
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(port_stubs, CONFIG_DJI_REMOTE_LOG_LEVEL);

void ui_pairing_add_discovered_camera(const char *name, const uint8_t *mac,
				      int8_t rssi, uint32_t device_id)
{
	LOG_INF("[stub] discovered camera: %s  %02X:%02X:%02X:%02X:%02X:%02X  "
		"rssi=%d device_id=0x%04X",
		name, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
		rssi, (unsigned int)device_id);
}

void ui_pairing_update_discovered_camera_name(const char *name, const uint8_t *mac)
{
	LOG_INF("[stub] camera name update: %s  %02X:%02X:%02X:%02X:%02X:%02X",
		name, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void connect_logic_mark_slot_found(int slot_index)
{
	LOG_INF("[stub] slot %d found during boot scan", slot_index);
}

bool connect_logic_is_slot_found(int slot_index)
{
	ARG_UNUSED(slot_index);
	return false;
}

void connect_logic_mark_slot_not_found_during_boot(int slot_index)
{
	LOG_INF("[stub] slot %d not found during boot scan", slot_index);
}
