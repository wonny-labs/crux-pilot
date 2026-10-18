#include "device_input.h"

#include <errno.h>
#include <stddef.h>
#include <zephyr/drivers/gpio.h>

#if DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
#endif
static bool initialised;

int controller_device_init(void) {
    initialised = false;
#if DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
    if (!gpio_is_ready_dt(&button)) {
        return -ENODEV;
    }
    int result = gpio_pin_configure_dt(&button, GPIO_INPUT);
    initialised = result == 0;
    return result;
#else
    return -ENODEV;
#endif
}

int controller_device_read(int *pressed) {
    if (pressed == NULL) {
        return -EINVAL;
    }
    if (!initialised) {
        return -ENODEV;
    }
#if DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
    int value = gpio_pin_get_dt(&button);
    if (value < 0) {
        return value;
    }
    *pressed = value;
    return 0;
#else
    return -ENODEV;
#endif
}
