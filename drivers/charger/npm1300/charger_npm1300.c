#define DT_DRV_COMPAT nordic_npm1300_charger_new_api

#include <zephyr/device.h>
#include <zephyr/drivers/charger.h>
#include <zephyr/drivers/mfd/npm1300.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/npm1300_charger.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(charger_npm1300, CONFIG_CHARGER_LOG_LEVEL);

#define STATUS_BATTERY_DETECTED 0x01
#define STATUS_COMPLETED 0x02
#define STATUS_TRICKLECHARGE 0x04
#define STATUS_CONSTANTCURRENT 0x08
#define STATUS_CONSTANTVOLTAGE 0x10
#define STATUS_RECHARGE 0x20
#define STATUS_DIE_TEMP_HIGH 0x40
#define STATUS_SUPPLEMENT_ACTIVE 0x80

#define VBUS_PRESENT 0x01

#define STATUS_CHARGING_MASK                                                                       \
    (STATUS_TRICKLECHARGE | STATUS_CONSTANTCURRENT | STATUS_CONSTANTVOLTAGE)

#define CHARGE_EVENT_MASK                                                                          \
    BIT(NPM1300_EVENT_CHG_COMPLETED) | BIT(NPM1300_EVENT_CHG_ERROR) |                              \
        BIT(NPM1300_EVENT_BATTERY_DETECTED) | BIT(NPM1300_EVENT_BATTERY_REMOVED) |                 \
        BIT(NPM1300_EVENT_VBUS_DETECTED) | BIT(NPM1300_EVENT_VBUS_REMOVED)

struct charger_npm1300_config {
    const struct device *mfd;
    const struct device *charger;
};

struct charger_npm1300_data {
    const struct device *dev;
    struct gpio_callback gpio_cb;
    struct k_work int_routine_work;
    charger_status_notifier_t status_notifier;
    charger_online_notifier_t online_notifier;
    enum charger_status status;
    enum charger_online online;
};

static int get_charger_channel(const struct device *dev, enum sensor_channel chan,
                               struct sensor_value *val) {
    const struct charger_npm1300_config *config = dev->config;

    int ret = sensor_sample_fetch(config->charger);
    if (ret) {
        return ret;
    }

    return sensor_channel_get(config->charger, chan, val);
}

static int get_charger_online(const struct device *dev, enum charger_online *val) {
    struct sensor_value sensor;
    int ret = get_charger_channel(dev, SENSOR_CHAN_NPM1300_CHARGER_VBUS_STATUS, &sensor);
    if (ret) {
        return ret;
    }

    *val = (sensor.val1 & VBUS_PRESENT) ? CHARGER_ONLINE_PROGRAMMABLE : CHARGER_ONLINE_OFFLINE;
    return 0;
}

static int get_battery_present(const struct device *dev, bool *val) {
    struct sensor_value sensor;
    int ret = get_charger_channel(dev, SENSOR_CHAN_NPM1300_CHARGER_STATUS, &sensor);
    if (ret) {
        return ret;
    }

    LOG_INF("CHARGER STATUS: %02x", sensor.val1);

    *val = (sensor.val1 & STATUS_BATTERY_DETECTED) != 0;
    return 0;
}

static int get_charger_status(const struct device *dev, enum charger_status *val) {
    struct sensor_value sensor;
    int ret = get_charger_channel(dev, SENSOR_CHAN_NPM1300_CHARGER_STATUS, &sensor);
    if (ret) {
        return ret;
    }

    if (sensor.val1 & STATUS_COMPLETED) {
        *val = CHARGER_STATUS_FULL;
    } else if (sensor.val1 & STATUS_CHARGING_MASK) {
        *val = CHARGER_STATUS_CHARGING;
        // TODO: My npm1300 does not report battery detected when VBUS is not present.
        // Is this expected or a hardware issue?
        // } else if (sensor.val1 & STATUS_BATTERY_DETECTED) {
        //     val->status = CHARGER_STATUS_DISCHARGING;
    } else {
        *val = CHARGER_STATUS_NOT_CHARGING;
    }

    return 0;
}

static int get_charge_type(const struct device *dev, enum charger_charge_type *val) {
    struct sensor_value sensor;
    int ret = get_charger_channel(dev, SENSOR_CHAN_NPM1300_CHARGER_STATUS, &sensor);
    if (ret) {
        return ret;
    }

    if (sensor.val1 & STATUS_TRICKLECHARGE) {
        *val = CHARGER_CHARGE_TYPE_TRICKLE;
    } else if (sensor.val1 & STATUS_CONSTANTCURRENT) {
        *val = CHARGER_CHARGE_TYPE_FAST;
    } else if (sensor.val1 & STATUS_CONSTANTVOLTAGE) {
        *val = CHARGER_CHARGE_TYPE_STANDARD;
    } else {
        *val = CHARGER_CHARGE_TYPE_NONE;
    }

    return 0;
}

static int charger_npm1300_get_prop(const struct device *dev, const charger_prop_t prop,
                                    union charger_propval *val) {
    struct charger_npm1300_data *data = dev->data;

    switch (prop) {
    case CHARGER_PROP_ONLINE:
        val->online = data->online;
        return 0;

    case CHARGER_PROP_PRESENT:
        return get_battery_present(dev, &val->present);

    case CHARGER_PROP_STATUS:
        return get_charger_status(dev, &val->status);

    case CHARGER_PROP_CHARGE_TYPE:
        return get_charge_type(dev, &val->charge_type);

    default:
        return -ENOTSUP;
    }
}

static int charger_npm1300_set_prop(const struct device *dev, const charger_prop_t prop,
                                    const union charger_propval *val) {
    struct charger_npm1300_data *data = dev->data;

    switch (prop) {
    case CHARGER_PROP_STATUS_NOTIFICATION:
        data->status_notifier = val->status_notification;
        return 0;

    case CHARGER_PROP_ONLINE_NOTIFICATION:
        data->online_notifier = val->online_notification;
        return 0;

    default:
        return -ENOTSUP;
    }
}

static int charger_npm1300_init_properties(const struct device *dev) {
    struct charger_npm1300_data *data = dev->data;

    int ret = get_charger_status(dev, &data->status);
    if (ret) {
        LOG_ERR("Failed to read charger status: %d", ret);
        return ret;
    }

    ret = get_charger_online(dev, &data->online);
    if (ret) {
        LOG_ERR("Failed to read charger online: %d", ret);
        return ret;
    }

    return 0;
}

static void charger_npm1300_interrupt_work_handler(struct k_work *work) {
    struct charger_npm1300_data *data =
        CONTAINER_OF(work, struct charger_npm1300_data, int_routine_work);
    const struct device *dev = data->dev;

    enum charger_status new_status;
    int ret = get_charger_status(dev, &new_status);

    if (ret) {
        LOG_ERR("Failed to read charger status: %d", ret);
    } else if (data->status != new_status) {
        LOG_DBG("Charger status = %d", new_status);

        data->status = new_status;

        if (data->status_notifier) {
            data->status_notifier(data->status);
        }
    }

    enum charger_online new_online;
    ret = get_charger_online(dev, &new_online);

    if (ret) {
        LOG_ERR("Failed to read charger online: %d", ret);
    } else if (data->online != new_online) {
        LOG_DBG("Charger online = %d", new_online);

        data->online = new_online;

        if (data->online_notifier) {
            data->online_notifier(data->online);
        }
    }
}

static void charger_npm1300_interrupt_callback(const struct device *dev, struct gpio_callback *cb,
                                               uint32_t pins) {
    LOG_DBG("Charger event: %08x", pins);

    struct charger_npm1300_data *data = CONTAINER_OF(cb, struct charger_npm1300_data, gpio_cb);

    k_work_submit(&data->int_routine_work);
}

static int charger_npm1300_init(const struct device *dev) {
    const struct charger_npm1300_config *config = dev->config;
    struct charger_npm1300_data *data = dev->data;

    if (!device_is_ready(config->mfd)) {
        LOG_ERR("MFD device is not ready");
        return -ENODEV;
    }

    if (!device_is_ready(config->charger)) {
        LOG_ERR("Charger device is not ready");
        return -ENODEV;
    }

    data->dev = dev;

    int ret = charger_npm1300_init_properties(dev);
    if (ret) {
        return ret;
    }

    k_work_init(&data->int_routine_work, charger_npm1300_interrupt_work_handler);

    gpio_init_callback(&data->gpio_cb, charger_npm1300_interrupt_callback, CHARGE_EVENT_MASK);

    return mfd_npm1300_add_callback(config->mfd, &data->gpio_cb);
}

static DEVICE_API(charger, charger_npm1300_api) = {
    .get_property = charger_npm1300_get_prop,
    .set_property = charger_npm1300_set_prop,
};

#define CHARGER_NPM1300_DEFINE_ALL(n)                                                              \
    static struct charger_npm1300_data charger_npm1300_data_##n;                                   \
    static const struct charger_npm1300_config charger_npm1300_config_##n = {                      \
        .mfd = DEVICE_DT_GET(DT_INST_PARENT(n)),                                                   \
        .charger = DEVICE_DT_GET(DT_INST_PHANDLE(n, charger)),                                     \
    };                                                                                             \
                                                                                                   \
    DEVICE_DT_INST_DEFINE(n, charger_npm1300_init, NULL, &charger_npm1300_data_##n,                \
                          &charger_npm1300_config_##n, POST_KERNEL, CONFIG_CHARGER_INIT_PRIORITY,  \
                          &charger_npm1300_api);

DT_INST_FOREACH_STATUS_OKAY(CHARGER_NPM1300_DEFINE_ALL)
