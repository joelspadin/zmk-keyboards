# ZMK Keyboards

ZMK configuration for my custom keyboards. See https://zmk.dev/docs/features/modules for details on how to use this module.

## Marten Numpad

Firmware for https://github.com/joelspadin/marten_numpad

Build with the board `marten_numpad`. The following shields are available for enabling optional features:

- TODO: shield for left encoder
- TODO: shield for right encoder
- TODO: shield for both encoders
- `marten_nice_view` - Adds a [nice!view](https://nicekeyboards.com/nice-view/) display.
- TODO: shield for OLED

Pressing the power button once will turn the keyboard off. It will not turn off if USB is connected.

Pressing the power button again or connecting USB will turn it back on.

### Status LEDs

Red "charge" LED:

| LED State | Meaning              |
| --------- | -------------------- |
| Solid     | Charging battery     |
| Off       | Not charging battery |

Blue "status" LED:

| LED State                                | Meaning                                     |
| ---------------------------------------- | ------------------------------------------- |
| Fading slowly in and out                 | In bootloader and connected to USB host     |
| Fading quickly in and out for ~3 seconds | In bootloader but no connection to USB host |
| Flashing quickly                         | Flashing firmware                           |
| Lights and fades out                     | Powering off                                |
| Blinks twice                             | Powering on                                 |
| Off                                      | Either operating normally or powered off    |

### Known Issues

#### Power usage increases by ~350 uA for the rest of the power cycle after flashing firmware.

Cause: not yet known. Maybe an issue with the bootloader?

Workaround: press the reset button or press the power button twice to cycle power.

## Indicator LED Driver

A driver for controlled LEDs based on HID indicator states. (I plan to get this into mainline ZMK after the Zephyr 4.1 upgrade is complete.)

To use this:

1. Add `#include <dt-bindings/zmk/hid_indicators.h>` to the top of your devicetree file.
2. Define an LED device that implements the [LED API](https://docs.zephyrproject.org/latest/hardware/peripherals/led.html), e.g. `gpio-leds` or `pwm-leds`.
3. Create a node with `compatible = "zmk,indicator-leds"`.
4. Create one child node per LED that you want to be controlled by an HID indicator.
5. For each child node, set the `leds` property to a list of LEDs that should be controlled.
   - This can be a single LED, e.g. `leds = <&led1>`, or multiple LEDs, e.g. `leds = <&led1 &led2>`.
6. For each child node, set the `indicator` property to the symbol from [hid_indicators.h](include/dt-bindings/zmk/hid_indicators.h) for the indicator by which it should be controlled.

```dts
#include <dt-bindings/zmk/hid_indicators.h>

/ {
    leds {
        compatible = "gpio-leds";

        caps_lock_led: caps_lock_led {
            gpios = <&gpio0 1 GPIO_ACTIVE_HIGH>;
        };
    };

    indicators {
        compatible = "zmk,indicator-leds";

        caps_lock_indicator: caps_lock {
            leds = <&caps_lock_led>;
            indicator = <HID_INDICATOR_CAPS_LOCK>;
        };
    };
};
```

Each child node of `zmk,indicator-leds` can have the following properties:

| Property              | Type     | Description                                                           | Default |
| --------------------- | -------- | --------------------------------------------------------------------- | ------- |
| `leds`                | phandles | Required: One or more LED devices to control                          |         |
| `indicator`           | int      | Required: The `HID_INDICATOR_*` value to indicate                     |         |
| `active-brightness`   | int      | LED brightness in percent when the indicator is active                | 100     |
| `inactive-brightness` | int      | LED brightness in percent when the indicator is not active            | 0       |
| `on-while-idle`       | bool     | Keep LEDs enabled even when the keyboard is idle and on battery power | false   |
