#include "consumer.h"

crux_status_t read_logical_input(bool *active)
{
    crux_status_t result = crux_gpio_configure_input(CRUX_GPIO_LINE_0);
    return result == CRUX_OK ? crux_gpio_read(CRUX_GPIO_LINE_0, active) : result;
}
