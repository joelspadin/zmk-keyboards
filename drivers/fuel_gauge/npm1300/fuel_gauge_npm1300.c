#define DT_DRV_COMPAT nordic_npm1300_fuel_gauge

#include <zephyr/device.h>
#include <zephyr/drivers/fuel_gauge.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/npm1300_charger.h>
#include <zephyr/drivers/mfd/npm1300.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(fuel_gauge_npm1300, CONFIG_FUEL_GAUGE_LOG_LEVEL);

#define STATUS_BATTERY_DETECTED 0x01
#define STATUS_COMPLETED 0x02
#define STATUS_TRICKLECHARGE 0x04
#define STATUS_CONSTANTCURRENT 0x08
#define STATUS_CONSTANTVOLTAGE 0x10
#define STATUS_RECHARGE 0x20
#define STATUS_DIE_TEMP_HIGH 0x40
#define STATUS_SUPPLEMENT_ACTIVE 0x80

struct fuel_gauge_npm1300_config {
    const struct device *mfd;
    const struct device *charger;
    // TODO: add method to set battery profile
};

static int get_charger_channel(const struct device *dev, enum sensor_channel chan,
                               struct sensor_value *val) {
    const struct fuel_gauge_npm1300_config *config = dev->config;

    int ret = sensor_sample_fetch(config->charger);
    if (ret) {
        return ret;
    }

    return sensor_channel_get(config->charger, chan, val);
}

static int get_avg_current(const struct device *dev, union fuel_gauge_prop_val *val) {
    struct sensor_value sensor;
    int ret = get_charger_channel(dev, SENSOR_CHAN_GAUGE_AVG_CURRENT, &sensor);
    if (ret) {
        return ret;
    }

    val->avg_current = sensor.val1 * 1000000 + sensor.val2;
    return 0;
}

static int get_battery_present(const struct device *dev, union fuel_gauge_prop_val *val) {
    struct sensor_value sensor;
    int ret = get_charger_channel(dev, SENSOR_CHAN_NPM1300_CHARGER_STATUS, &sensor);
    if (ret) {
        return ret;
    }

    // TODO: requires
    // https://github.com/zephyrproject-rtos/zephyr/commit/9a8e4663ac35212e35fa28374116078202a3cb19
    // val->present_state = (sensor.val1 & STATUS_BATTERY_DETECTED) != 0;

    return -ENOTSUP;
}

static int get_battery_voltage(const struct device *dev, union fuel_gauge_prop_val *val) {
    struct sensor_value sensor;
    int ret = get_charger_channel(dev, SENSOR_CHAN_GAUGE_VOLTAGE, &sensor);
    if (ret) {
        return ret;
    }

    val->voltage = sensor.val1 * 1000000 + sensor.val2;
    return 0;
}

// TODO: replace this with something based on the battery profile data
// Can I pull in nrfxlib and use Nordic's proprietary fuel gauge library?
// https://github.com/nrfconnect/sdk-nrf/blob/main/samples/pmic/native/npm13xx_fuel_gauge/src/fuel_gauge.c
static uint8_t lithium_ion_mv_to_pct(int16_t bat_mv) {
    // Simple linear approximation of a battery based off adafruit's discharge graph:
    // https://learn.adafruit.com/li-ion-and-lipoly-batteries/voltages

    if (bat_mv >= 4200) {
        return 100;
    } else if (bat_mv <= 3450) {
        return 0;
    }

    return bat_mv * 2 / 15 - 459;
}

static int get_battery_percent(const struct device *dev, union fuel_gauge_prop_val *val) {
    union fuel_gauge_prop_val voltage;
    int ret = get_battery_voltage(dev, &voltage);
    if (ret) {
        return ret;
    }

    val->absolute_state_of_charge = lithium_ion_mv_to_pct(voltage.voltage / 1000);
    return 0;
}

static int fuel_gauge_npm1300_get_prop(const struct device *dev, fuel_gauge_prop_t prop,
                                       union fuel_gauge_prop_val *val) {
    switch (prop) {
    case FUEL_GAUGE_AVG_CURRENT:
        return get_avg_current(dev, val);

    case FUEL_GAUGE_ABSOLUTE_STATE_OF_CHARGE:
    case FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE:
        return get_battery_percent(dev, val);

    case FUEL_GAUGE_PRESENT_STATE:
        return get_battery_present(dev, val);

    case FUEL_GAUGE_VOLTAGE:
        return get_battery_voltage(dev, val);

    default:
        return -ENOTSUP;
    }
}

static int fuel_gauge_npm1300_set_prop(const struct device *dev, fuel_gauge_prop_t prop,
                                       union fuel_gauge_prop_val val) {
    return -ENOTSUP;
}

static int fuel_gauge_npm1300_init(const struct device *dev) {
    const struct fuel_gauge_npm1300_config *config = dev->config;

    if (!device_is_ready(config->mfd)) {
        LOG_ERR("MFD device is not ready");
        return -ENODEV;
    }

    if (!device_is_ready(config->charger)) {
        LOG_ERR("Charger device is not ready");
        return -ENODEV;
    }

    return 0;
}

static DEVICE_API(fuel_gauge, fuel_gauge_npm1300_api) = {
    .get_property = fuel_gauge_npm1300_get_prop,
    .set_property = fuel_gauge_npm1300_set_prop,
};

#define FUEL_GAUGE_NPM1300_DEFINE_ALL(n)                                                           \
    static const struct fuel_gauge_npm1300_config fuel_gauge_npm1300_config##n = {                 \
        .mfd = DEVICE_DT_GET(DT_INST_PARENT(n)),                                                   \
        .charger = DEVICE_DT_GET(DT_INST_PHANDLE(n, charger)),                                     \
    };                                                                                             \
                                                                                                   \
    DEVICE_DT_INST_DEFINE(n, fuel_gauge_npm1300_init, NULL, NULL, &fuel_gauge_npm1300_config##n,   \
                          POST_KERNEL, CONFIG_FUEL_GAUGE_INIT_PRIORITY, &fuel_gauge_npm1300_api);

DT_INST_FOREACH_STATUS_OKAY(FUEL_GAUGE_NPM1300_DEFINE_ALL)