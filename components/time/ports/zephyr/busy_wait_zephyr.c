#include "busy_wait_port.h"
#include <zephyr/kernel.h>

crux_status_t crux_busy_wait_impl_ns(int64_t duration_ns)
{
    if (duration_ns < 0) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (duration_ns == 0) {
        return CRUX_OK;
    }
#if !defined(CONFIG_SYS_CLOCK_EXISTS) || defined(CONFIG_TIMER_READS_ITS_FREQUENCY_AT_RUNTIME) ||   \
    defined(CONFIG_SYSTEM_CLOCK_HW_CYCLES_PER_SEC_RUNTIME_UPDATE) || defined(CONFIG_SMP) ||        \
    (defined(CONFIG_ARCH_HAS_CUSTOM_BUSY_WAIT) && !defined(CONFIG_BOARD_NATIVE_SIM))
    /* Defensive direct-provider check; enabled unsupported compositions fail CMake. */
    return CRUX_ERR_NOT_SUPPORTED;
#else
    uint32_t hz = sys_clock_hw_cycles_per_sec();
    if (hz == 0) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    /* ceil(us * hz / 1e6) <= INT32_MAX leaves a half-counter observation window.
     * These are provider-local input bounds, not an operational spin budget. */
    uint64_t maximum_us = (uint64_t)INT32_MAX * UINT64_C(1000000) / hz;
    if (maximum_us > UINT32_MAX) {
        maximum_us = UINT32_MAX;
    }
    uint64_t us = (uint64_t)(duration_ns / INT64_C(1000)) + (duration_ns % INT64_C(1000) != 0);
    if (us > maximum_us) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    /* No Crux IRQ masking around the wait; native observations restore their
     * brief critical sections individually. Preserve the caller's IRQ state. */
    k_busy_wait((uint32_t)us);
    return CRUX_OK;
#endif
}
