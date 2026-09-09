/*
 * Pro Micro nRF52840 Hardware Abstraction Layer Header
 *
 * Same API shape as the original m5stack_basic_v27_hal.h so app_main.c can
 * keep its HAL_* macro indirection unchanged.
 *
 * Unlike the ESP-IDF HALs, no pin numbers are defined here: every pin lives in
 * boards/promicro_nrf52840_common.dtsi and is reached through devicetree
 * aliases (btn-a, btn-b, btn-c, display-bl, status-led) and the
 * chosen zephyr,display node.
 */

#ifndef PROMICRO_NRF52840_HAL_H
#define PROMICRO_NRF52840_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

/* Display resolution -- 240x320 ST7789V panel driven in landscape,
 * matching the original M5Stack Basic V2.7 layout */
#define PROMICRO_LCD_H_RES  320
#define PROMICRO_LCD_V_RES  240

/* Function prototypes */

/**
 * @brief Initialize all Pro Micro nRF52840 hardware
 * @return ESP_OK on success, error code otherwise
 */
int promicro_nrf52840_init(void);

/**
 * @brief Initialize display
 * @return ESP_OK on success, error code otherwise
 */
int promicro_nrf52840_display_init(void);

/**
 * @brief Initialize buttons
 * @return ESP_OK on success, error code otherwise
 */
int promicro_nrf52840_buttons_init(void);

/**
 * @brief Check if button A is pressed
 * @return true if pressed, false otherwise
 */
bool promicro_nrf52840_button_a_pressed(void);

/**
 * @brief Check if button B is pressed
 * @return true if pressed, false otherwise
 */
bool promicro_nrf52840_button_b_pressed(void);

/**
 * @brief Check if button C is pressed
 * @return true if pressed, false otherwise
 */
bool promicro_nrf52840_button_c_pressed(void);

/**
 * @brief Set display brightness
 * @param brightness Brightness level (0-255)
 */
void promicro_nrf52840_display_set_brightness(uint8_t brightness);

/**
 * @brief Turn the display backlight on
 *
 * Called by app_main after the LVGL splash screen has been rendered,
 * ensuring the first visible frame is the splash (see ADR-010).
 */
void promicro_nrf52840_backlight_on(void);

/**
 * @brief Bring up LVGL on the display
 *
 * Zephyr's LVGL integration creates the display, its rendering buffers and
 * the workqueue that runs lv_timer_handler() from devicetree and Kconfig, so
 * this only has to make sure LVGL is initialised and hand back the display.
 *
 * @return LVGL display handle, or NULL on failure
 */
lv_display_t *promicro_nrf52840_lvgl_init(void);

/* Color definitions
 *
 * Carried over from m5stack_basic_v27_hal.h unchanged. The values are only
 * used as tokens -- ui_show_shutter_bottom_message() maps them to LVGL
 * colors -- so the original panel's GBR channel order does not matter here.
 */
#define M5_COLOR_BLACK      0x0000
#define M5_COLOR_WHITE      0xFFFF
#define M5_COLOR_RED        0x07E0  // GBR: G=63 appears as RED on screen
#define M5_COLOR_GREEN      0x001F  // GBR: B=31 appears as GREEN on screen
#define M5_COLOR_BLUE       0xF800  // GBR: R=31 appears as BLUE on screen
#define M5_COLOR_YELLOW     0x07FF  // GBR: G+B appears as YELLOW on screen
#define M5_COLOR_CYAN       0xF81F  // GBR: R+B appears as CYAN on screen
#define M5_COLOR_MAGENTA    0xFFE0  // GBR: R+G appears as MAGENTA on screen
#define M5_COLOR_ORANGE     0xFDA0
#define M5_COLOR_PURPLE     0x8010  // Purple (approximate for GBR)
#define M5_COLOR_DARKGREY   0x39C6  // Dark grey for indicators
#define M5_COLOR_GREY       0x7BEF  // Medium grey for text
#define M5_COLOR_GRAY       0x8410  // Button background gray (r=128, g=128, b=128)

#endif /* PROMICRO_NRF52840_HAL_H */
