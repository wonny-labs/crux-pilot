/* Fault injection for the Zephyr-direct reboot service, so that the suites that wrap
 * crux_mutex_lock() keep their source and their assertions.
 *
 * The service locks a k_mutex, and k_mutex_lock() is an inline function that calls
 * z_impl_k_mutex_lock(), so wrapping crux_mutex_lock() no longer reaches it. This file wraps
 * z_impl_k_mutex_lock() instead and tells the service's state lock by its address, because other
 * code in the same image takes kernel mutexes too. For that one lock it asks the suite's own
 * __wrap_crux_mutex_lock() what to do, with the wait that the service asked for. The suite's
 * wrapper decides by the length of the wait, as it did before, and a lock that it lets through is
 * taken for real. The suite is linked with -Wl,--wrap=crux_mutex_lock
 * -Wl,--wrap=z_impl_k_mutex_lock and is the same file on the Host (over the stub of
 * tests/reboot/host_kernel) and on native_sim. */
#include <crux/status.h>
#include <crux/sync.h>

#include <zephyr/kernel.h>

#include <errno.h>
#include <stdint.h>

struct k_mutex *crux_reboot_test_state_lock(void);

// NOLINTBEGIN(bugprone-reserved-identifier): GNU linker wrapping ABI
crux_status_t __wrap_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms);
int __real_z_impl_k_mutex_lock(struct k_mutex *mutex, k_timeout_t timeout);
int __wrap_z_impl_k_mutex_lock(struct k_mutex *mutex, k_timeout_t timeout);
// NOLINTEND(bugprone-reserved-identifier)

int __wrap_z_impl_k_mutex_lock(struct k_mutex *mutex, k_timeout_t timeout)
{
    static crux_mutex_t proxy;
    int64_t wait;

    if (mutex != crux_reboot_test_state_lock()) {
        return __real_z_impl_k_mutex_lock(mutex, timeout);
    }
    if (proxy.slot == 0u && proxy.generation == 0u && crux_mutex_create(&proxy) != CRUX_OK) {
        return -EINVAL;
    }
    wait = timeout.ticks < 0 ? (int64_t)UINT32_MAX : k_ticks_to_ms_floor64(timeout.ticks);
    /* The suite's wrapper answers for a lock of this wait. Passed through, it takes the proxy,
     * which nothing else uses, at once; the proxy is released and the service's own lock is taken.
     */
    if (__wrap_crux_mutex_lock(proxy, (uint32_t)wait) != CRUX_OK) {
        return timeout.ticks == 0 ? -EBUSY : -EAGAIN;
    }
    (void)crux_mutex_unlock(proxy);
    return __real_z_impl_k_mutex_lock(mutex, timeout);
}
