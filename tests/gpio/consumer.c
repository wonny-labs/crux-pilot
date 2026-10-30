#include "consumer.h"

crux_status_t gpio_consumer_read_input(bool *active)
{
    crux_status_t result = crux_gpio_configure(CRUX_GPIO_LINE_0, CRUX_GPIO_INPUT);
    return result == CRUX_OK ? crux_gpio_read(CRUX_GPIO_LINE_0, active) : result;
}
