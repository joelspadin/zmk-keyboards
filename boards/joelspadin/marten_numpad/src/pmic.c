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

#include <stdlib.h>

#define ERRLOG_BASE 0x0EU
#define ERRLOG_OFFSET_TASKCLRERRLOG 0x00U
#define ERRLOG_OFFSET_RSTCAUSE 0x03U

#define VBUS_BASE 0x02U
#define VBUS_OFFSET_STATUS 0x07U

// VBUS status masks
#define STATUS_PRESENT_MASK 0x01U

static const struct device *regulators = DEVICE_DT_GET(DT_NODELABEL(npm1300_regulators));

static const struct device *pmic = DEVICE_DT_GET(DT_NODELABEL(npm1300_pmic));

static const struct led_dt_spec status_led = LED_DT_SPEC_GET(DT_NODELABEL(status_led));

#if IS_ENABLED(CONFIG_BOARD_POWER_BUTTON_SOFT_OFF)

static bool vbus_present = false;

static void vbus_present_init(void) {
    uint8_t status;
    const int err = mfd_npm1300_reg_read(pmic, VBUS_BASE, VBUS_OFFSET_STATUS, &status);

    if (err) {
        printk("Failed to read VBUS status: %d\n", err);
        return false;
    }

    vbus_present = (status & STATUS_PRESENT_MASK) != 0;
}

static void handle_vbus_event(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    if (pins & BIT(NPM1300_EVENT_VBUS_DETECTED)) {
        vbus_present = true;
    }
    if (pins & BIT(NPM1300_EVENT_VBUS_REMOVED)) {
        vbus_present = false;
    }
}

static void fade_status_led(void) {
    for (int brightness = 50; brightness >= 0; brightness--) {
        led_set_brightness_dt(&status_led, brightness);
        k_sleep(K_MSEC(10));
    }
}

static void enter_ship_mode(struct k_work *work) {
    if (vbus_present) {
        printk("Cannot enter ship mode while on USB power\n");
        return;
    }

    zmk_endpoint_clear_reports();

    // Indicate that we are powering off. This also gives enough time to finish
    // clearing the endpoint.
    fade_status_led();

    const int err = regulator_parent_ship_mode(regulators);
    if (err) {
        printk("Failed to enter ship mode: %d\n", err);
    }
}

K_WORK_DEFINE(ship_mode_work, enter_ship_mode);

static void handle_button_event(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    k_work_submit(&ship_mode_work);
}

#endif

static void handle_blink_work(struct k_work *work);
static void handle_blink_timer(struct k_timer *timer);

K_WORK_DEFINE(blink_work, handle_blink_work);
K_TIMER_DEFINE(blink_timer, handle_blink_timer, NULL);

// TODO: could make this fade in and out a bit so it's not as harsh.
static void handle_blink_work(struct k_work *work) {
    static bool led_state = false;
    static int led_blink_count = 2;

    if (led_state) {
        led_off_dt(&status_led);
        led_state = false;
        led_blink_count--;
    } else {
        led_set_brightness_dt(&status_led, 50);
        led_state = true;
    }

    if (led_blink_count <= 0) {
        k_timer_stop(&blink_timer);
    }
}

static void handle_blink_timer(struct k_timer *timer) { k_work_submit(&blink_work); }

static uint8_t get_reset_cause(void) {
    uint8_t reset_cause;
    int err = mfd_npm1300_reg_read(pmic, ERRLOG_BASE, ERRLOG_OFFSET_RSTCAUSE, &reset_cause);
    if (err) {
        printk("Failed to get reset cause: %d\n", err);
        return 0;
    }

    err = mfd_npm1300_reg_write(pmic, ERRLOG_BASE, ERRLOG_OFFSET_TASKCLRERRLOG, 1U);
    if (err) {
        printk("Failed to clear error log: %d\n", err);
    }

    return reset_cause;
}

static int marten_numpad_pmic_init(void) {
    if (!led_is_ready_dt(&status_led)) {
        printk("LED not ready\n");
        return 0;
    }

    if (!device_is_ready(pmic)) {
        printk("PMIC not ready.\n");
        return 0;
    }

    if (!device_is_ready(regulators)) {
        printk("Regulator not ready\n");
        return 0;
    }

#if IS_ENABLED(CONFIG_BOARD_POWER_BUTTON_SOFT_OFF)
    static struct gpio_callback shiphold_cb;
    static struct gpio_callback vbus_cb;

    gpio_init_callback(&shiphold_cb, handle_button_event, BIT(NPM1300_EVENT_SHIPHOLD_PRESS));
    gpio_init_callback(&vbus_cb, handle_vbus_event,
                       BIT(NPM1300_EVENT_VBUS_DETECTED) | BIT(NPM1300_EVENT_VBUS_REMOVED));

    mfd_npm1300_add_callback(pmic, &shiphold_cb);
    mfd_npm1300_add_callback(pmic, &vbus_cb);

    vbus_present_init();
#endif

    // If we reset due to some PMIC event, flash the status LED to indicate that
    // we are powered back on. If we reset due to the nRF52's reset button, the
    // bootloader will flash the LED on its own, so we don't need to do it again.
    if (get_reset_cause() != 0) {
        k_timer_start(&blink_timer, K_NO_WAIT, K_MSEC(150));
    }

    return 0;
}

SYS_INIT(marten_numpad_pmic_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
