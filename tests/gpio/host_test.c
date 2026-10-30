#include <crux/gpio.h>

#include "consumer.h"
#include "gpio_host_test.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int main(void)
{
    bool active = true;

    assert(crux_gpio_read(CRUX_GPIO_LINE_0, NULL) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_gpio_configure(UINT32_MAX, CRUX_GPIO_INPUT) == CRUX_ERR_OUT_OF_RANGE);
    assert(crux_gpio_configure(CRUX_GPIO_LINE_0, (crux_gpio_direction_t)99) ==
           CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_gpio_read(CRUX_GPIO_LINE_0, &active) == CRUX_ERR_NOT_READY);
    assert(active);

    assert(crux_gpio_configure(CRUX_GPIO_LINE_0, CRUX_GPIO_INPUT) == CRUX_OK);
    assert(crux_gpio_host_test_drive(CRUX_GPIO_LINE_0, true) == CRUX_OK);
    active = false;
    assert(gpio_consumer_read_input(&active) == CRUX_OK);
    assert(active);
    crux_gpio_host_test_fail_reads(true);
    active = false;
    assert(crux_gpio_read(CRUX_GPIO_LINE_0, &active) == CRUX_ERR_IO);
    assert(!active);
    crux_gpio_host_test_fail_reads(false);
    assert(crux_gpio_write(CRUX_GPIO_LINE_0, false) == CRUX_ERR_NOT_SUPPORTED);

    assert(crux_gpio_configure(CRUX_GPIO_LINE_0, CRUX_GPIO_OUTPUT) == CRUX_OK);
    assert(crux_gpio_write(CRUX_GPIO_LINE_0, true) == CRUX_OK);
    assert(crux_gpio_host_test_output(CRUX_GPIO_LINE_0, &active) == CRUX_OK);
    assert(active);
    assert(crux_gpio_read(CRUX_GPIO_LINE_0, &active) == CRUX_ERR_NOT_SUPPORTED);

    return 0;
}
