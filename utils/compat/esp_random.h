/*
 * ESP-IDF hardware RNG compatibility shim.
 *
 * The nRF52840 has a hardware RNG behind Zephyr's entropy driver, which
 * sys_rand32_get() uses when CONFIG_ENTROPY_GENERATOR is enabled.
 */

#ifndef __ESP_RANDOM_H__
#define __ESP_RANDOM_H__

#include <stdint.h>

#include <zephyr/random/random.h>

static inline uint32_t esp_random(void)
{
	return sys_rand32_get();
}

#endif /* __ESP_RANDOM_H__ */
