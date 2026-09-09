#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>

#include "freertos_compat.h"
#include "esp_log.h"

#include "connect_logic.h"
#include "status_logic.h"

#define TAG "LOGIC_LIGHT"

/* Zephyr log module -- replaces the ESP-IDF per-tag log level */
LOG_MODULE_REGISTER(light_logic, CONFIG_DJI_REMOTE_LOG_LEVEL);

/*
 * Status LED.
 *
 * The original boards carry a WS2812 driven over the ESP32's RMT peripheral,
 * so the state machine below picks an RGB colour.  The Pro Micro nRF52840 has
 * a single-colour LED and the nRF52840 has no RMT, so the colour is collapsed
 * to a brightness; the blink patterns still distinguish searching from
 * recording, which is what the LED is read for in practice.
 *
 * The pin comes from the board devicetree (pwm-led0 alias -> P0.15).
 */
static const struct pwm_dt_spec status_led = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led0));

/* PWM period for the status LED; 1 kHz is well above the flicker threshold */
#define LED_PERIOD_NS 1000000U

// Initialize status LED related configurations and settings
static void init_rgb_led(void) {
    if (!pwm_is_ready_dt(&status_led)) {
        ESP_LOGE(TAG, "Status LED PWM not ready");
        return;
    }

    pwm_set_dt(&status_led, LED_PERIOD_NS, 0); // Turn the LED off
    ESP_LOGI(TAG, "Status LED initialized");
}

// Set status LED colour -- the single-colour LED shows the brightest channel
static void set_rgb_color(uint8_t red, uint8_t green, uint8_t blue) {
    uint8_t level = red;

    if (green > level) {
        level = green;
    }
    if (blue > level) {
        level = blue;
    }

    pwm_set_dt(&status_led, LED_PERIOD_NS,
               (LED_PERIOD_NS * (uint32_t)level) / 255U);
}

// Initialize variables needed for RGB LED status
uint8_t led_red = 0, led_green = 0, led_blue = 0;   // RGB values
bool led_blinking = false;                          // Whether to blink
bool current_led_on = false;                        // Current LED status (on or off)

// Function to update LED state
static void update_led_state() {
    connect_state_t current_connect_state = connect_logic_get_state();
    bool current_camera_recording = is_camera_recording();

    // Print current status
    // ESP_LOGI(TAG, "Current connect state: %d, Camera recording: %d", current_connect_state, current_camera_recording);

    led_blinking = false;  // LED does not blink by default

    switch (current_connect_state) {
        case BLE_NOT_INIT:
            led_red = 13;      // 255 * 0.05
            led_green = 0;
            led_blue = 0;      // Red indicates Bluetooth not initialized
            break;

        case BLE_INIT_COMPLETE:
            led_red = 13;      // 255 * 0.05
            led_green = 13;    // 255 * 0.05
            led_blue = 0;      // Yellow indicates Bluetooth initialization complete
            break;

        case BLE_SEARCHING:
            led_blinking = true;
            led_red = 0;
            led_green = 0;
            led_blue = 13;     // 255 * 0.05, Blue blinking indicates Bluetooth is searching
            break;

        case BLE_CONNECTED:
            led_red = 0;
            led_green = 0;
            led_blue = 13;     // 255 * 0.05, Blue indicates Bluetooth is connected
            break;

        case PROTOCOL_CONNECTED:
            if (current_camera_recording) {
                led_blinking = true;
                led_red = 0;
                led_green = 13;    // 255 * 0.05
                led_blue = 0;      // Green blinking indicates recording
            } else {
                led_red = 0;
                led_green = 13;    // 255 * 0.05
                led_blue = 0;      // Green indicates not recording
            }
            break;
            
        default:
            led_red = 0;
            led_green = 0;
            led_blue = 0;  // Turn off LED
            break;
    }
}

// Timer callback function for periodic LED state updates
static void led_state_timer_callback(TimerHandle_t xTimer) {
    update_led_state();
}

// Timer callback function for LED blinking effect
static void led_blink_timer_callback(TimerHandle_t xTimer) {
    if (led_blinking) {
        // If in blinking state and LED is currently on, turn it off
        if (current_led_on) {
            set_rgb_color(0, 0, 0);  // Turn off LED
        } else {
            // If LED is currently off, set it to RGB color
            set_rgb_color(led_red, led_green, led_blue);
        }
        current_led_on = !current_led_on;
    } else {
        // If not in blinking state, directly set to RGB color
        set_rgb_color(led_red, led_green, led_blue);
    }
}

// Initialize light logic, including LED state updates and blink timer
int init_light_logic() {
    init_rgb_led();
    
    // Create a timer that executes update_led_state every 500ms
    TimerHandle_t led_state_timer = xTimerCreate("led_state_timer", pdMS_TO_TICKS(500), pdTRUE, (void *)0, led_state_timer_callback);

    if (led_state_timer != NULL) {
        xTimerStart(led_state_timer, 0);
        ESP_LOGI(TAG, "LED state timer started successfully");
    } else {
        ESP_LOGE(TAG, "Failed to create LED state timer");
        return -1;
    }

    // Create another timer to control LED blinking state (on/off)
    TimerHandle_t led_blink_timer = xTimerCreate("led_blink_timer", pdMS_TO_TICKS(500), pdTRUE, (void *)0, led_blink_timer_callback);

    if (led_blink_timer != NULL) {
        xTimerStart(led_blink_timer, 0);
        ESP_LOGI(TAG, "LED blink timer started successfully");
        return 0;
    } else {
        ESP_LOGE(TAG, "Failed to create LED blink timer");
        return -1;
    }
}