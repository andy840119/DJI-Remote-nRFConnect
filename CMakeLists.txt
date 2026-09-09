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
)

target_sources(app PRIVATE
    src/main.c
    src/port_stubs.c
    utils/compat/esp_err.c
    utils/compat/freertos_compat.c
    utils/compat/nvs_flash.c
    utils/crc/custom_crc16.c
    utils/crc/custom_crc32.c
    protocol/dji_protocol_parser.c
    protocol/dji_protocol_data_processor.c
    protocol/dji_protocol_data_descriptors.c
    protocol/dji_protocol_data_structures.c
    ble/ble.c
    data/data.c
    main/promicro_nrf52840_hal.c
)
