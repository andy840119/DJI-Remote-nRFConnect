# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(dji_remote)

# The directory layout mirrors the original ESP-IDF project
# (https://github.com/rhoenschrat/DJI-Remote) so both trees can be diffed.
target_include_directories(app PRIVATE
    utils/compat
    utils/crc
    protocol
    ble
    data
    main
    logic
    gps
    .
)

target_sources(app PRIVATE
    utils/compat/esp_err.c
    utils/compat/freertos_compat.c
    utils/compat/nvs_flash.c
    utils/compat/uart_compat.c
    utils/crc/custom_crc16.c
    utils/crc/custom_crc32.c
    protocol/dji_protocol_parser.c
    protocol/dji_protocol_data_processor.c
    protocol/dji_protocol_data_descriptors.c
    protocol/dji_protocol_data_structures.c
    ble/ble.c
    data/data.c
    main/promicro_nrf52840_hal.c
    logic/connect_logic.c
    logic/command_logic.c
    logic/status_logic.c
    logic/enums_logic.c
    logic/light_logic.c
    gps/gps_reader.c
    log_config.c
    main/app_main.c
    main/ui.c
    main/ui_layout.c
    main/ui_screen_main.c
    main/ui_screen_pairing.c
    main/ui_screen_settings.c
    main/ui_screen_mode_switch.c
    main/ui_screen_splash.c
    main/icons.c
    main/lvgl_icons.c
    main/splash_logo.c
)
