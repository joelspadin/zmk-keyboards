#include <zmk/event_manager.h>
#include <zmk/events/hid_indicators_changed.h>

#include <zephyr/kernel.h>

int test_listener(const zmk_event_t *eh) {
    struct zmk_hid_indicators_changed *ev;

    if ((ev = as_zmk_hid_indicators_changed(eh)) != NULL) {
        printk("HID indicators: 0x%02x\n", ev->indicators);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(hid_indicators, test_listener);
ZMK_SUBSCRIPTION(hid_indicators, zmk_hid_indicators_changed);
