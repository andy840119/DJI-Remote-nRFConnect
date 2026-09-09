#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define LED0_NODE DT_ALIAS(led0)

#if !DT_NODE_HAS_STATUS(LED0_NODE, okay)
#error "Unsupported board: led0 alias is not defined"
#endif

static const struct gpio_dt_spec led =
    GPIO_DT_SPEC_GET(LED0_NODE, gpios);

int main(void)
{
    int ret;

    printk("\n");
    printk("=================================\n");
    printk("Application Started\n");
    printk("=================================\n");

    if (!gpio_is_ready_dt(&led)) {
        printk("LED GPIO not ready\n");
        return 0;
    }

    ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        printk("GPIO configure failed: %d\n", ret);
        return 0;
    }

    printk("LED configured\n");

    /* 開機先快速閃 10 次 */
    for (int i = 0; i < 10; i++) {
        gpio_pin_toggle_dt(&led);
        k_msleep(100);
    }

    printk("Entering main loop\n");

    while (1) {
        gpio_pin_toggle_dt(&led);
        printk("toggle\n");
        k_msleep(1000);
    }

    return 0;
}