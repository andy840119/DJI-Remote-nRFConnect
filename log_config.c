/*
 * Central Logging Configuration Implementation
 *
 * Implements the logging configuration functions that control the global log
 * level. ESP-IDF could do this with a single esp_log_level_set("*", level);
 * Zephyr filters per log source, so the level is applied to every registered
 * source of the default domain instead.
 */

#include <zephyr/logging/log_ctrl.h>

#include "log_config.h"
#include "esp_log.h"

/* Current application log level */
static app_log_level_t s_app_log_level = APP_DEFAULT_LOG_LEVEL;

void app_log_set_level(app_log_level_t level) {
    /* Store the level */
    s_app_log_level = level;

    /* Map app_log_level_t to a Zephyr log level */
    uint32_t z_level;
    switch (level) {
        case APP_LOG_LEVEL_NONE:
            z_level = LOG_LEVEL_NONE;
            break;
        case APP_LOG_LEVEL_ERROR:
            z_level = LOG_LEVEL_ERR;
            break;
        case APP_LOG_LEVEL_WARN:
            z_level = LOG_LEVEL_WRN;
            break;
        case APP_LOG_LEVEL_INFO:
            z_level = LOG_LEVEL_INF;
            break;
        case APP_LOG_LEVEL_DEBUG:
        case APP_LOG_LEVEL_VERBOSE:
            /* Zephyr has no separate verbose level; both map to debug */
            z_level = LOG_LEVEL_DBG;
            break;
        default:
            /* Fallback to INFO if invalid level */
            z_level = LOG_LEVEL_INF;
            s_app_log_level = APP_LOG_LEVEL_INFO;
            break;
    }

    /* Apply to every log source of the default domain -- the equivalent of
     * ESP-IDF's "*" wildcard tag. Requires CONFIG_LOG_RUNTIME_FILTERING. */
    if (IS_ENABLED(CONFIG_LOG_RUNTIME_FILTERING)) {
        for (int16_t src = 0; src < (int16_t)log_src_cnt_get(Z_LOG_LOCAL_DOMAIN_ID); src++) {
            log_filter_set(NULL, Z_LOG_LOCAL_DOMAIN_ID, src, z_level);
        }
    }
}

app_log_level_t app_log_get_level(void) {
    return s_app_log_level;
}
