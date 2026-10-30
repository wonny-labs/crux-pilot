#include "gpio_port.h"

#include <stddef.h>

static bool configured;
static bool input_active;
static bool output_active;
static bool fail_reads;
static crux_gpio_direction_t direction;

static crux_status_t line_check(crux_gpio_line_t line)
{
    return line == CRUX_GPIO_LINE_0 ? CRUX_OK : CRUX_ERR_OUT_OF_RANGE;
}

crux_status_t crux_gpio_port_configure(crux_gpio_line_t line, crux_gpio_direction_t requested)
{
    crux_status_t result = line_check(line);
    if (result != CRUX_OK) {
        return result;
    }
    configured = true;
    direction = requested;
    if (requested == CRUX_GPIO_OUTPUT) {
        output_active = false;
    }
    return CRUX_OK;
}

crux_status_t crux_gpio_port_read(crux_gpio_line_t line, bool *active)
{
    crux_status_t result = line_check(line);
    if (result != CRUX_OK) {
        return result;
    }
    if (!configured) {
        return CRUX_ERR_NOT_READY;
    }
    if (direction != CRUX_GPIO_INPUT) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    if (fail_reads) {
        return CRUX_ERR_IO;
    }
    *active = input_active;
    return CRUX_OK;
}

crux_status_t crux_gpio_port_write(crux_gpio_line_t line, bool active)
{
    crux_status_t result = line_check(line);
    if (result != CRUX_OK) {
        return result;
    }
    if (!configured) {
        return CRUX_ERR_NOT_READY;
    }
    if (direction != CRUX_GPIO_OUTPUT) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    output_active = active;
    return CRUX_OK;
}

crux_status_t crux_gpio_host_test_drive(crux_gpio_line_t line, bool active)
{
    crux_status_t result = line_check(line);
    if (result != CRUX_OK) {
        return result;
    }
    if (!configured) {
        return CRUX_ERR_NOT_READY;
    }
    if (direction != CRUX_GPIO_INPUT) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    input_active = active;
    return CRUX_OK;
}

crux_status_t crux_gpio_host_test_output(crux_gpio_line_t line, bool *active)
{
    crux_status_t result = line_check(line);
    if (result != CRUX_OK) {
        return result;
    }
    if (active == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (!configured) {
        return CRUX_ERR_NOT_READY;
    }
    if (direction != CRUX_GPIO_OUTPUT) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    *active = output_active;
    return CRUX_OK;
}

void crux_gpio_host_test_fail_reads(bool fail)
{
    fail_reads = fail;
}
