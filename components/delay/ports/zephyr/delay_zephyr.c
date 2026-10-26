#include "delay_port.h"
#include <stddef.h>
#include <zephyr/kernel.h>

static crux_status_t max_microseconds(uint32_t *out) {
#if !defined(CONFIG_SYS_CLOCK_EXISTS) || defined(CONFIG_TIMER_READS_ITS_FREQUENCY_AT_RUNTIME) ||   \
    defined(CONFIG_SYSTEM_CLOCK_HW_CYCLES_PER_SEC_RUNTIME_UPDATE) || defined(CONFIG_SMP) ||        \
    (defined(CONFIG_ARCH_HAS_CUSTOM_BUSY_WAIT) && !defined(CONFIG_BOARD_NATIVE_SIM))
    /* Only stable single-core generic counters and the native_sim model are
     * covered by this provider profile; do not use the no-timer crude loop. */
    (void)out;
    return CRUX_ERR_NOT_SUPPORTED;
#else
    uint32_t hz = sys_clock_hw_cycles_per_sec();
    if (hz == 0)
        return CRUX_ERR_NOT_SUPPORTED;
    /* Keep a half-counter observation window after the threshold, rather than
     * relying on sampling a tiny interval immediately before uint32 wrap.
     * ceil(us * hz / 1e6) <= INT32_MAX, and the native argument is uint32 us. */
    uint64_t limit = (uint64_t)INT32_MAX * UINT64_C(1000000) / hz;
    if (limit > UINT32_MAX)
        limit = UINT32_MAX;
    *out = (uint32_t)limit;
    return CRUX_OK;
#endif
}

crux_status_t crux_delay_port_get_max_ns(int64_t *out) {
    if (out == NULL)
        return CRUX_ERR_INVALID_ARGUMENT;
    uint32_t us;
    crux_status_t status = max_microseconds(&us);
    if (status == CRUX_OK)
        *out = (int64_t)us * INT64_C(1000);
    return status;
}

crux_status_t crux_delay_port_wait_ns(int64_t duration_ns) {
    if (duration_ns < 0)
        return CRUX_ERR_INVALID_ARGUMENT;
    if (duration_ns == 0)
        return CRUX_OK;
    uint32_t maximum_us;
    crux_status_t status = max_microseconds(&maximum_us);
    if (status != CRUX_OK)
        return status;
    /* Quotient/remainder ceiling avoids overflow at INT64_MAX. */
    uint64_t us = (uint64_t)(duration_ns / INT64_C(1000)) + (duration_ns % INT64_C(1000) != 0);
    if (us > maximum_us)
        return CRUX_ERR_OUT_OF_RANGE;
    /* No Crux IRQ masking around the wait. Native counter reads restore their
     * short critical sections individually. Caller interrupt state is retained. */
    k_busy_wait((uint32_t)us);
    return CRUX_OK;
}
