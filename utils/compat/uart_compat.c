/*
 * ESP-IDF UART driver compatibility shim -- implementation.
 *
 * A Zephyr interrupt-driven UART feeding a ring buffer, which uart_read_bytes()
 * drains.  This is what ESP-IDF's driver does internally and what gps_reader.c
 * expects.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/ring_buffer.h>

#include "driver/uart.h"

LOG_MODULE_REGISTER(uart_compat, CONFIG_DJI_REMOTE_LOG_LEVEL);

/* The GPS module is the only UART peripheral in the design */
static const struct device *s_uart = DEVICE_DT_GET(DT_ALIAS(gps_uart));

/* Sized for a second of NMEA at 9600 baud (~960 bytes) */
#define UART_RX_RING_SIZE 1024

RING_BUF_DECLARE(s_rx_ring, UART_RX_RING_SIZE);

static struct k_sem s_rx_sem;
static bool s_installed;

static void uart_isr(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	uint8_t buf[32];

	if (!uart_irq_update(dev)) {
		return;
	}

	while (uart_irq_rx_ready(dev)) {
		int read = uart_fifo_read(dev, buf, sizeof(buf));

		if (read <= 0) {
			break;
		}

		uint32_t written = ring_buf_put(&s_rx_ring, buf, (uint32_t)read);

		if (written < (uint32_t)read) {
			/* Same behaviour as the ESP-IDF driver: the oldest data
			 * that no longer fits is simply lost. */
			LOG_WRN("RX ring buffer full, dropped %u bytes",
				(unsigned int)(read - written));
		}

		k_sem_give(&s_rx_sem);
	}
}

esp_err_t uart_driver_install(uart_port_t uart_num, int rx_buffer_size,
			      int tx_buffer_size, int queue_size, void *uart_queue,
			      int intr_alloc_flags)
{
	ARG_UNUSED(uart_num);
	ARG_UNUSED(rx_buffer_size);
	ARG_UNUSED(tx_buffer_size);
	ARG_UNUSED(queue_size);
	ARG_UNUSED(uart_queue);
	ARG_UNUSED(intr_alloc_flags);

	if (s_installed) {
		return ESP_OK;
	}

	if (!device_is_ready(s_uart)) {
		LOG_ERR("GPS UART device not ready");
		return ESP_FAIL;
	}

	k_sem_init(&s_rx_sem, 0, 1);
	ring_buf_reset(&s_rx_ring);

	int rc = uart_irq_callback_user_data_set(s_uart, uart_isr, NULL);

	if (rc != 0) {
		LOG_ERR("cannot set UART callback: %d", rc);
		return ESP_FAIL;
	}

	uart_irq_rx_enable(s_uart);
	s_installed = true;
	return ESP_OK;
}

esp_err_t uart_driver_delete(uart_port_t uart_num)
{
	ARG_UNUSED(uart_num);

	if (!s_installed) {
		return ESP_OK;
	}

	uart_irq_rx_disable(s_uart);
	ring_buf_reset(&s_rx_ring);
	s_installed = false;
	return ESP_OK;
}

esp_err_t uart_param_config(uart_port_t uart_num, const uart_config_t *uart_config)
{
	ARG_UNUSED(uart_num);

	if (uart_config == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	struct uart_config cfg = {
		.baudrate = uart_config->baud_rate,
		.parity = (uart_config->parity == UART_PARITY_EVEN)  ? UART_CFG_PARITY_EVEN
			  : (uart_config->parity == UART_PARITY_ODD) ? UART_CFG_PARITY_ODD
								     : UART_CFG_PARITY_NONE,
		.stop_bits = (uart_config->stop_bits == UART_STOP_BITS_2)
				     ? UART_CFG_STOP_BITS_2
				     : UART_CFG_STOP_BITS_1,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = (uart_config->flow_ctrl == UART_HW_FLOWCTRL_DISABLE)
				     ? UART_CFG_FLOW_CTRL_NONE
				     : UART_CFG_FLOW_CTRL_RTS_CTS,
	};

	int rc = uart_configure(s_uart, &cfg);

	if (rc == -ENOSYS || rc == -ENOTSUP) {
		/* Runtime reconfiguration is not compiled in; the devicetree
		 * baud rate applies. */
		LOG_WRN("UART runtime configuration unavailable, keeping the devicetree settings");
		return ESP_OK;
	}
	if (rc != 0) {
		LOG_ERR("uart_configure failed: %d", rc);
		return ESP_FAIL;
	}
	return ESP_OK;
}

/* Pins come from devicetree on Zephyr */
esp_err_t uart_set_pin(uart_port_t uart_num, int tx_io_num, int rx_io_num,
		       int rts_io_num, int cts_io_num)
{
	ARG_UNUSED(uart_num);
	ARG_UNUSED(tx_io_num);
	ARG_UNUSED(rx_io_num);
	ARG_UNUSED(rts_io_num);
	ARG_UNUSED(cts_io_num);
	return ESP_OK;
}

int uart_read_bytes(uart_port_t uart_num, void *buf, uint32_t length,
		    uint32_t ticks_to_wait)
{
	ARG_UNUSED(uart_num);

	if (buf == NULL || length == 0) {
		return 0;
	}
	if (!s_installed) {
		return -1;
	}

	uint32_t read = ring_buf_get(&s_rx_ring, buf, length);

	if (read > 0) {
		return (int)read;
	}

	/* Nothing buffered yet -- wait for the ISR to signal new data */
	if (ticks_to_wait > 0 && k_sem_take(&s_rx_sem, K_MSEC(ticks_to_wait)) == 0) {
		read = ring_buf_get(&s_rx_ring, buf, length);
	}

	return (int)read;
}

esp_err_t uart_get_buffered_data_len(uart_port_t uart_num, size_t *size)
{
	ARG_UNUSED(uart_num);

	if (size == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	*size = ring_buf_size_get(&s_rx_ring);
	return ESP_OK;
}

esp_err_t uart_flush(uart_port_t uart_num)
{
	ARG_UNUSED(uart_num);

	ring_buf_reset(&s_rx_ring);
	return ESP_OK;
}
