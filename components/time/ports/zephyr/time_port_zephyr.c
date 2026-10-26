#include "time_port.h"
#include <stddef.h>
#include <time.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/clock.h>

#if defined(CONFIG_MULTITHREADING)
K_MUTEX_DEFINE(realtime_lock);
#endif

bool crux_time_port_is_thread(void)
{
    return !k_is_in_isr();
}
crux_status_t crux_time_port_lock(void)
{
#if defined(CONFIG_MULTITHREADING)
    return k_mutex_lock(&realtime_lock, K_FOREVER) == 0 ? CRUX_OK : CRUX_ERR_IO;
#else
    /* ISR entry is rejected, so the sole execution context cannot interleave. */
    return CRUX_OK;
#endif
}
void crux_time_port_unlock(void)
{
#if defined(CONFIG_MULTITHREADING)
    (void)k_mutex_unlock(&realtime_lock);
#endif
}
crux_status_t crux_time_port_init(void)
{
    return IS_ENABLED(CONFIG_SYS_CLOCK_EXISTS) ? CRUX_OK : CRUX_ERR_NOT_SUPPORTED;
}
crux_status_t crux_time_port_read(crux_realtime_t *out)
{
    struct timespec value;
    if (out == NULL)
        return CRUX_ERR_INVALID_ARGUMENT;
    if (!IS_ENABLED(CONFIG_SYS_CLOCK_EXISTS))
        return CRUX_ERR_NOT_SUPPORTED;
    if (sys_clock_gettime(SYS_CLOCK_REALTIME, &value) != 0)
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
    if (!IS_ENABLED(CONFIG_SYS_CLOCK_EXISTS))
        return CRUX_ERR_NOT_SUPPORTED;
    const struct timespec native = {(time_t)value->seconds, (long)value->nanoseconds};
    if ((int64_t)native.tv_sec != value->seconds)
        return CRUX_ERR_OUT_OF_RANGE;
    return sys_clock_settime(SYS_CLOCK_REALTIME, &native) == 0 ? CRUX_OK : CRUX_ERR_IO;
}
