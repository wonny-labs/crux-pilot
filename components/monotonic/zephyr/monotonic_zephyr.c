#include "monotonic_port.h"

#include <zephyr/kernel.h>

BUILD_ASSERT(IS_ENABLED(CONFIG_TIMEOUT_64BIT), "Monotonic requires 64-bit absolute timeouts");

bool crux_monotonic_port_is_thread(void) {
    return !k_is_in_isr();
}

crux_status_t crux_monotonic_port_now(int64_t *out_ms) {
    return crux_monotonic_ticks_to_ms(k_uptime_ticks(), CONFIG_SYS_CLOCK_TICKS_PER_SEC, out_ms);
}

crux_status_t crux_monotonic_port_wait_until(int64_t deadline_ms) {
    int64_t ticks;
    crux_status_t status =
        crux_monotonic_ms_to_ticks_ceil(deadline_ms, CONFIG_SYS_CLOCK_TICKS_PER_SEC, &ticks);
    if (status != CRUX_OK) {
        return status;
    }
    /* K_TIMEOUT_ABS_TICKS clamps at INT64_MAX-1. Reject instead of silently
     * shortening an unrepresentable absolute wait or producing K_FOREVER. */
    if (ticks > INT64_MAX - 1) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    (void)k_sleep(K_TIMEOUT_ABS_TICKS(ticks));
    /* k_wakeup may return early; the common predicate checks the same deadline. */
    return CRUX_OK;
}
