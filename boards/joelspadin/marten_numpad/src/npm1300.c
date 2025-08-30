#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led.h>
#include <zephyr/drivers/mfd/npm1300.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/npm1300_charger.h>
#include <zephyr/dt-bindings/regulator/npm1300.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <zmk/endpoints.h>
#include <zmk/usb.h>

#include <stdlib.h>

#define SHIP_BASE 0x0BU

#define SHIP_OFFSET_SHPHLD_CFG_STROBE 0x01U
#define SHIP_OFFSET_RESET_CFG 0x03U
#define SHIP_OFFSET_SHPHLD_CFG 0x04U

static const struct device *regulator = DEVICE_DT_GET(DT_NODELABEL(main_regulator));

static const struct device *pmic = DEVICE_DT_GET(DT_NODELABEL(npm1300_pmic));

static const struct led_dt_spec status_led = LED_DT_SPEC_GET(DT_NODELABEL(status_led));

#if IS_ENABLED(CONFIG_BOARD_POWER_BUTTON_SOFT_OFF)
static void power_button_callback(const struct device *dev, struct gpio_callback *cb,
                                  uint32_t pins) {
    if (zmk_usb_is_powered()) {
        printk("Cannot enter ship hold mode while USB is connected\n");
        return;
    }

    printk("Power button pressed. Powering off\n");

    // TODO: will this trigger immediately on wake? May need to wait for the power
    // button to be released before enabling this callback.

    zmk_endpoints_clear_current();
    k_sleep(K_MSEC(100));

    for (int i = 0; i < 3; i++) {
        led_on_dt(&status_led);
        k_sleep(K_MSEC(150));

        led_off_dt(&status_led);
        k_sleep(K_MSEC(150));
    }

    int err = regulator_parent_ship_mode(regulator);
    if (err) {
        printk("Failed to power off: %d", err);
    }
}
#endif

static int marten_numpad_npm1300_init(void) {
    if (!device_is_ready(pmic)) {
        printk("PMIC device not ready.\n");
        return 0;
    }

    if (!device_is_ready(regulator)) {
        printk("Regulator device not ready\n");
        return 0;
    }

    if (!led_is_ready_dt(&status_led)) {
        printk("LED device not ready\n");
        return 0;
    }

    printk("Initialized\n");

#if IS_ENABLED(CONFIG_BOARD_POWER_BUTTON_SOFT_OFF)
    // Setup callback for shiphold button press
    static struct gpio_callback event_cb;

    gpio_init_callback(&event_cb, power_button_callback, BIT(NPM1300_EVENT_SHIPHOLD_PRESS));

    mfd_npm1300_add_callback(pmic, &event_cb);
#endif

    return 0;
}

SYS_INIT(marten_numpad_npm1300_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);