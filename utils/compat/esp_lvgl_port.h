/*
 * esp_lvgl_port compatibility shim.
 *
 * The original firmware uses Espressif's `esp_lvgl_port` component, which owns
 * the LVGL tick/handler task and hands out a recursive lock that every LVGL
 * call from outside that task must take.
 *
 * Zephyr's LVGL integration provides exactly the same two things:
 * - CONFIG_LV_Z_RUN_LVGL_ON_WORKQUEUE runs lv_timer_handler() on its own
 *   workqueue
 * - CONFIG_LV_Z_LVGL_MUTEX exposes lvgl_lock() / lvgl_unlock()
 *
 * so the shim is a straight rename and the UI code keeps its
 * lvgl_port_lock() / lvgl_port_unlock() pairs unchanged.
 */

#ifndef __ESP_LVGL_PORT_H__
#define __ESP_LVGL_PORT_H__

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>
#include <lvgl_zephyr.h>

/**
 * @brief Take the LVGL lock.
 *
 * @param timeout_ms Ignored; Zephyr's lvgl_lock() always waits. Kept so the
 *                   original call sites (which pass 0) compile unchanged.
 * @return always true -- the lock is never contended to the point of failure
 */
static inline bool lvgl_port_lock(uint32_t timeout_ms)
{
	(void)timeout_ms;
	lvgl_lock();
	return true;
}

/** @brief Release the LVGL lock. */
static inline void lvgl_port_unlock(void)
{
	lvgl_unlock();
}

#endif /* __ESP_LVGL_PORT_H__ */
