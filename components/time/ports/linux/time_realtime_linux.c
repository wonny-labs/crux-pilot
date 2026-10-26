#include "time_port.h"
#include <stddef.h>
#include <time.h>

crux_status_t crux_time_port_init(void)
{
    /* Init never reads, adopts or changes the native system clock. */
    return CRUX_OK;
}

crux_status_t crux_time_port_read(crux_realtime_t *out)
{
    struct timespec value;
    if (out == NULL)
        return CRUX_ERR_INVALID_ARGUMENT;
    if (clock_gettime(CLOCK_REALTIME, &value) != 0)
        return CRUX_ERR_IO;
    if (value.tv_nsec < 0 || value.tv_nsec >= 1000000000L)
        return CRUX_ERR_IO;
    *out = (crux_realtime_t){(int64_t)value.tv_sec, (uint32_t)value.tv_nsec};
    return CRUX_OK;
}

crux_status_t crux_time_port_write(const crux_realtime_t *value)
{
    if (value == NULL)
        return CRUX_ERR_INVALID_ARGUMENT;
    const struct timespec native = {(time_t)value->seconds, (long)value->nanoseconds};
    if ((int64_t)native.tv_sec != value->seconds)
        return CRUX_ERR_OUT_OF_RANGE;
    return clock_settime(CLOCK_REALTIME, &native) == 0 ? CRUX_OK : CRUX_ERR_IO;
}
