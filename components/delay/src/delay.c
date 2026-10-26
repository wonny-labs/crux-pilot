#include "delay_port.h"
#include <crux/delay.h>
#include <stddef.h>

static crux_status_t read_limit(int64_t *out)
{
    int64_t value;
    crux_status_t status = crux_delay_port_get_max_ns(&value);
    if (status != CRUX_OK)
        return status;
    if (value < 0)
        return CRUX_ERR_IO;
    *out = value;
    return CRUX_OK;
}

crux_status_t crux_delay_get_max_duration(crux_duration_t *out)
{
    if (out == NULL)
        return CRUX_ERR_INVALID_ARGUMENT;
    int64_t limit;
    crux_status_t status = read_limit(&limit);
    if (status == CRUX_OK)
        *out = crux_duration_from_ns(limit);
    return status;
}

crux_status_t crux_delay_busy_wait(crux_duration_t duration)
{
    int64_t ns = crux_duration_to_ns(duration);
    if (ns < 0)
        return CRUX_ERR_INVALID_ARGUMENT;
    if (ns == 0)
        return CRUX_OK;
    int64_t limit;
    crux_status_t status = read_limit(&limit);
    if (status != CRUX_OK)
        return status;
    if (ns > limit)
        return CRUX_ERR_OUT_OF_RANGE;
    return crux_delay_port_wait_ns(ns);
}
