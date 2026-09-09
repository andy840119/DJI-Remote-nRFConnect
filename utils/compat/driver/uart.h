/*
 * ESP-IDF UART driver compatibility shim.
 *
 * gps_reader.c drives the GPS module through ESP-IDF's UART driver: install a
 * driver with an RX ring buffer, then poll it with uart_read_bytes().  Zephyr's
 * UART API is interrupt- or async-callback based and has no such buffer, so the
 * shim owns a ring buffer fed from the RX interrupt and serves reads from it.
 *
 * Only one port is supported: the `gps-uart` devicetree alias.  Pin assignment
 * comes from devicetree, so uart_set_pin() is accepted and ignored.
 */

#ifndef __COMPAT_DRIVER_UART_H__
#define __COMPAT_DRIVER_UART_H__

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/* Port numbers are accepted for source compatibility; every port maps to the
 * gps-uart devicetree alias. */
typedef int uart_port_t;

#define UART_NUM_0 0
#define UART_NUM_1 1
#define UART_NUM_2 2

#define UART_PIN_NO_CHANGE (-1)

typedef enum {
	UART_DATA_5_BITS = 0,
	UART_DATA_6_BITS,
	UART_DATA_7_BITS,
	UART_DATA_8_BITS,
} uart_word_length_t;

typedef enum {
	UART_PARITY_DISABLE = 0,
	UART_PARITY_EVEN,
	UART_PARITY_ODD,
} uart_parity_t;

typedef enum {
	UART_STOP_BITS_1 = 0,
	UART_STOP_BITS_1_5,
	UART_STOP_BITS_2,
} uart_stop_bits_t;

typedef enum {
	UART_HW_FLOWCTRL_DISABLE = 0,
	UART_HW_FLOWCTRL_RTS,
	UART_HW_FLOWCTRL_CTS,
	UART_HW_FLOWCTRL_CTS_RTS,
} uart_hw_flowcontrol_t;

/* Clock source selection is an ESP32 concept; accepted and ignored */
typedef enum {
	UART_SCLK_DEFAULT = 0,
	UART_SCLK_APB,
} uart_sclk_t;

typedef struct {
	int baud_rate;
	uart_word_length_t data_bits;
	uart_parity_t parity;
	uart_stop_bits_t stop_bits;
	uart_hw_flowcontrol_t flow_ctrl;
	uint8_t rx_flow_ctrl_thresh;
	uart_sclk_t source_clk;
} uart_config_t;

esp_err_t uart_driver_install(uart_port_t uart_num, int rx_buffer_size,
			      int tx_buffer_size, int queue_size, void *uart_queue,
			      int intr_alloc_flags);
esp_err_t uart_driver_delete(uart_port_t uart_num);

esp_err_t uart_param_config(uart_port_t uart_num, const uart_config_t *uart_config);
esp_err_t uart_set_pin(uart_port_t uart_num, int tx_io_num, int rx_io_num,
		       int rts_io_num, int cts_io_num);

/**
 * @brief Read up to `length` bytes, waiting at most `ticks_to_wait` ms.
 * @return number of bytes read, or -1 on error
 */
int uart_read_bytes(uart_port_t uart_num, void *buf, uint32_t length,
		    uint32_t ticks_to_wait);

esp_err_t uart_get_buffered_data_len(uart_port_t uart_num, size_t *size);
esp_err_t uart_flush(uart_port_t uart_num);

#endif /* __COMPAT_DRIVER_UART_H__ */
