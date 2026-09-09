/*
 * Pro Micro nRF52840 Hardware Abstraction Layer
 *
 * Replaces m5stack_basic_v27_hal.c / waveshare_s3_lcd19_hal.c.  Those files
 * had to drive the SPI bus and the ST7789 panel by hand through esp_lcd; on
 * Zephyr the panel is a devicetree node handled by the in-tree `st7789v`
 * driver and LVGL is wired to it by the LVGL module, so this HAL only has to:
 *
 * - wait for the display device to be ready and unblank it
 * - drive the backlight PWM (was an ESP32 LEDC channel)
 * - read the three buttons (was GPIO polling with pull-ups)
 * - hand the LVGL display back to app_main
 *
 * Every pin comes from boards/promicro_nrf52840_common.dtsi.
 */

#include <stdint.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/display.h>

#include <lvgl.h>
#include <lvgl_zephyr.h>

#include "esp_log.h"
#include "promicro_nrf52840_hal.h"

#define TAG "PROMICRO_HAL"

/* Zephyr log module -- replaces the ESP-IDF per-tag log level */
LOG_MODULE_REGISTER(promicro_nrf52840_hal, CONFIG_DJI_REMOTE_LOG_LEVEL);

/* PWM period for the backlight. 1 kHz is well above the flicker threshold and
 * matches the LEDC frequency the original used. */
#define BACKLIGHT_PERIOD_NS 1000000U

static const struct device *s_display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

static const struct pwm_dt_spec s_backlight = PWM_DT_SPEC_GET(DT_ALIAS(display_bl));

static const struct gpio_dt_spec s_btn_a = GPIO_DT_SPEC_GET(DT_ALIAS(btn_a), gpios);
static const struct gpio_dt_spec s_btn_b = GPIO_DT_SPEC_GET(DT_ALIAS(btn_b), gpios);
static const struct gpio_dt_spec s_btn_c = GPIO_DT_SPEC_GET(DT_ALIAS(btn_c), gpios);

static bool s_backlight_ready;

/* ----------------------------------------------------------------
 *  Display
 * ---------------------------------------------------------------- */

int promicro_nrf52840_display_init(void)
{
	if (!device_is_ready(s_display)) {
		ESP_LOGE(TAG, "Display device not ready");
		return ESP_FAIL;
	}

	/* Keep the panel blanked until app_main has rendered the splash screen,
	 * so the first visible frame is the logo and not a white screen. */
	display_blanking_on(s_display);

	if (!pwm_is_ready_dt(&s_backlight)) {
		ESP_LOGW(TAG, "Backlight PWM not ready, continuing without dimming");
	} else {
		s_backlight_ready = true;
		/* Backlight off until promicro_nrf52840_backlight_on() */
		pwm_set_dt(&s_backlight, BACKLIGHT_PERIOD_NS, 0);
	}

	ESP_LOGI(TAG, "Display initialized (%dx%d)", PROMICRO_LCD_H_RES, PROMICRO_LCD_V_RES);
	return ESP_OK;
}

void promicro_nrf52840_display_set_brightness(uint8_t brightness)
{
	if (!s_backlight_ready) {
		return;
	}

	uint32_t pulse = (BACKLIGHT_PERIOD_NS * (uint32_t)brightness) / 255U;

	int rc = pwm_set_dt(&s_backlight, BACKLIGHT_PERIOD_NS, pulse);
	if (rc != 0) {
		ESP_LOGW(TAG, "Failed to set backlight brightness: %d", rc);
	}
}

void promicro_nrf52840_backlight_on(void)
{
	display_blanking_off(s_display);
	promicro_nrf52840_display_set_brightness(255);
	ESP_LOGI(TAG, "Backlight on");
}

/* ----------------------------------------------------------------
 *  Buttons
 * ---------------------------------------------------------------- */

static int button_init(const struct gpio_dt_spec *btn, const char *name)
{
	if (!gpio_is_ready_dt(btn)) {
		ESP_LOGE(TAG, "Button %s GPIO not ready", name);
		return ESP_FAIL;
	}

	int rc = gpio_pin_configure_dt(btn, GPIO_INPUT);
	if (rc != 0) {
		ESP_LOGE(TAG, "Failed to configure button %s: %d", name, rc);
		return ESP_FAIL;
	}
	return ESP_OK;
}

int promicro_nrf52840_buttons_init(void)
{
	if (button_init(&s_btn_a, "A") != ESP_OK ||
	    button_init(&s_btn_b, "B") != ESP_OK ||
	    button_init(&s_btn_c, "C") != ESP_OK) {
		return ESP_FAIL;
	}

	ESP_LOGI(TAG, "Buttons initialized");
	return ESP_OK;
}

/* The devicetree marks the buttons active-low with a pull-up, so
 * gpio_pin_get_dt() already returns 1 for "pressed". */
bool promicro_nrf52840_button_a_pressed(void)
{
	return gpio_pin_get_dt(&s_btn_a) > 0;
}

bool promicro_nrf52840_button_b_pressed(void)
{
	return gpio_pin_get_dt(&s_btn_b) > 0;
}

bool promicro_nrf52840_button_c_pressed(void)
{
	return gpio_pin_get_dt(&s_btn_c) > 0;
}

/* ----------------------------------------------------------------
 *  LVGL
 * ---------------------------------------------------------------- */

lv_display_t *promicro_nrf52840_lvgl_init(void)
{
	/* With CONFIG_LV_Z_AUTO_INIT the LVGL module has already run lvgl_init()
	 * during system init; calling it again is harmless but pointless, so
	 * only the display handle is fetched here. */
	lv_display_t *disp = lv_display_get_default();

	if (disp == NULL) {
		ESP_LOGE(TAG, "No default LVGL display");
		return NULL;
	}

	ESP_LOGI(TAG, "LVGL display initialized (%dx%d)",
		 (int)lv_display_get_horizontal_resolution(disp),
		 (int)lv_display_get_vertical_resolution(disp));
	return disp;
}

/* ----------------------------------------------------------------
 *  Top-level init
 * ---------------------------------------------------------------- */

int promicro_nrf52840_init(void)
{
	int res = promicro_nrf52840_display_init();

	if (res != ESP_OK) {
		return res;
	}

	res = promicro_nrf52840_buttons_init();
	if (res != ESP_OK) {
		return res;
	}

	ESP_LOGI(TAG, "Pro Micro nRF52840 hardware initialized");
	return ESP_OK;
}
