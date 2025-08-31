#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/charger.h>
#include <zephyr/drivers/fuel_gauge.h>

#include <zmk/event_manager.h>
#include <zmk/events/hid_indicators_changed.h>
#include <stdio.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static const struct device *fuel_gauge = DEVICE_DT_GET(DT_NODELABEL(npm1300_fuel_gauge));
static const struct device *charger = DEVICE_DT_GET(DT_NODELABEL(npm1300_charger_wrapper));

int test_listener(const zmk_event_t *eh) {
    struct zmk_hid_indicators_changed *ev;

    if ((ev = as_zmk_hid_indicators_changed(eh)) != NULL) {
        LOG_DBG("HID indicators: 0x%02x", ev->indicators);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(hid_indicators, test_listener);
ZMK_SUBSCRIPTION(hid_indicators, zmk_hid_indicators_changed);

static const char *status_name(enum charger_status status) {
    switch (status) {
    case CHARGER_STATUS_UNKNOWN:
        return "unknown";
    case CHARGER_STATUS_CHARGING:
        return "charging";
    case CHARGER_STATUS_DISCHARGING:
        return "discharging";
    case CHARGER_STATUS_NOT_CHARGING:
        return "not charging";
    case CHARGER_STATUS_FULL:
        return "full";
    }

    return "?";
}

static const char *charge_type_name(enum charger_charge_type type) {
    switch (type) {
    case CHARGER_CHARGE_TYPE_UNKNOWN:
        return "unknown";
    case CHARGER_CHARGE_TYPE_NONE:
        return "none";
    case CHARGER_CHARGE_TYPE_TRICKLE:
        return "trickle";
    case CHARGER_CHARGE_TYPE_FAST:
        return "fast";
    case CHARGER_CHARGE_TYPE_STANDARD:
        return "standard";
    case CHARGER_CHARGE_TYPE_ADAPTIVE:
        return "adaptive";
    case CHARGER_CHARGE_TYPE_LONGLIFE:
        return "long life";
    case CHARGER_CHARGE_TYPE_BYPASS:
        return "bypass";
    }

    return "?";
}

#define VAL_BUFFER_LEN 80
typedef char val_buffer_t[VAL_BUFFER_LEN];

typedef void (*print_fuel_prop_t)(const union fuel_gauge_prop_val *, val_buffer_t);
typedef void (*print_charger_prop_t)(const union charger_propval *, val_buffer_t);

static void print_avg_current(const union fuel_gauge_prop_val *val, val_buffer_t buf) {
    snprintf(buf, VAL_BUFFER_LEN, "%0.3f mA", (double)val->avg_current / 1000.0);
}

static void print_battery_percent(const union fuel_gauge_prop_val *val, val_buffer_t buf) {
    snprintf(buf, VAL_BUFFER_LEN, "%u%%", val->absolute_state_of_charge);
}

static void print_charger_online(const union charger_propval *val, val_buffer_t buf) {
    snprintf(buf, VAL_BUFFER_LEN, "%s", val->online == CHARGER_ONLINE_OFFLINE ? "no" : "yes");
}

static void print_voltage(const union fuel_gauge_prop_val *val, val_buffer_t buf) {
    snprintf(buf, VAL_BUFFER_LEN, "%0.3f V", (double)val->voltage / 1000000);
}

static void print_battery_present(const union charger_propval *val, val_buffer_t buf) {
    snprintf(buf, VAL_BUFFER_LEN, "%s", val->present ? "yes" : "no");
}

static void print_charger_status(const union charger_propval *val, val_buffer_t buf) {
    snprintf(buf, VAL_BUFFER_LEN, "%s", status_name(val->status));
}

static void print_charge_type(const union charger_propval *val, val_buffer_t buf) {
    snprintf(buf, VAL_BUFFER_LEN, "%s", charge_type_name(val->charge_type));
}

static void print_fuel_gauge_prop(const char *name, enum fuel_gauge_prop_type prop,
                                  print_fuel_prop_t print_val) {
    val_buffer_t buf;
    union fuel_gauge_prop_val val;
    int err = fuel_gauge_get_prop(fuel_gauge, prop, &val);
    if (err) {
        snprintf(buf, VAL_BUFFER_LEN, "error %d", err);
    } else {
        print_val(&val, buf);
    }

    LOG_INF("%s: %s", name, buf);
}

static void print_charger_prop(const char *name, enum charger_property prop,
                               print_charger_prop_t print_val) {
    val_buffer_t buf;
    union charger_propval val;
    int err = charger_get_prop(charger, prop, &val);
    if (err) {
        snprintf(buf, VAL_BUFFER_LEN, "error %d", err);
    } else {
        print_val(&val, buf);
    }

    LOG_INF("%s: %s", name, buf);
}

static void handle_timer_work(struct k_work *work) {
    LOG_INF("=== npm1300 status ===");

    print_fuel_gauge_prop("State of charge", FUEL_GAUGE_ABSOLUTE_STATE_OF_CHARGE,
                          print_battery_percent);
    print_fuel_gauge_prop("Avg current", FUEL_GAUGE_AVG_CURRENT, print_avg_current);
    print_fuel_gauge_prop("Voltage", FUEL_GAUGE_VOLTAGE, print_voltage);

    print_charger_prop("Online", CHARGER_PROP_ONLINE, print_charger_online);
    print_charger_prop("Present", CHARGER_PROP_PRESENT, print_battery_present);
    print_charger_prop("Status", CHARGER_PROP_STATUS, print_charger_status);
    print_charger_prop("Type", CHARGER_PROP_CHARGE_TYPE, print_charge_type);
}

K_WORK_DEFINE(timer_work, handle_timer_work);

static void handle_periodic_timer(struct k_timer *timer) { k_work_submit(&timer_work); }

K_TIMER_DEFINE(periodic_timer, handle_periodic_timer, NULL);

int test_fuel_gauge_init(void) {
    if (!device_is_ready(fuel_gauge)) {
        printk("Fuel gauge not ready\n");
        return 0;
    }

    if (!device_is_ready(charger)) {
        printk("Charger not ready\n");
        return 0;
    }

    // k_timer_start(&periodic_timer, K_SECONDS(1), K_SECONDS(5));

    return 0;
}

SYS_INIT(test_fuel_gauge_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
