#include "system_sim_watchdog.h"
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>

#if !defined(CONFIG_BOARD_NATIVE_SIM)
#error "CRUX_SYSTEM_NATIVE_SIM_WATCHDOG is a native_sim test setting, refused on other boards"
#endif

/* Simulated System watchdog for native_sim tests, a Zephyr watchdog device object in the sense of
 * ADR-0001 Decision 3's emulators. It is not hardware evidence and models nothing of a real
 * watchdog but one channel and its timeout in simulated kernel time. It accepts the call sequence
 * of the shared System provider as it is: one channel, window minimum 0, RESET_SOC only.
 *
 * On expiry it prints one line and calls sys_reboot, the path crux_system_reset takes. With
 * CONFIG_NATIVE_SIM_REBOOT the simulated process then restarts and reads the reset cause SOFTWARE,
 * never WATCHDOG (the native HWINFO supports POR and SOFTWARE only). */

static struct k_spinlock state_lock;
static struct k_work_delayable expiry_work;
static bool installed, running;
static uint32_t timeout_ms;
static int64_t armed_at_ms;

static void expired(struct k_work *work)
{
    ARG_UNUSED(work);
    printk("Crux simulated watchdog expired: no feed for %u ms; restarting the simulated process "
           "(after %lld ms)\n",
           (unsigned)timeout_ms, (long long)(k_uptime_get() - armed_at_ms));
    sys_reboot(SYS_REBOOT_COLD);
}

static int arm(uint32_t period)
{
    armed_at_ms = k_uptime_get();
    return k_work_reschedule(&expiry_work, K_MSEC(period));
}

static int sim_install_timeout(const struct device *dev, const struct wdt_timeout_cfg *cfg)
{
    ARG_UNUSED(dev);
    if (cfg == NULL || cfg->flags != WDT_FLAG_RESET_SOC || cfg->callback != NULL) {
        return -ENOTSUP;
    }
    if (cfg->window.min != 0 || cfg->window.max == 0) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    int result = 0;
    if (running) {
        result = -EBUSY;
    } else if (installed) {
        result = -ENOMEM;
    } else {
        installed = true;
        timeout_ms = cfg->window.max;
    }
    k_spin_unlock(&state_lock, key);
    return result; /* channel 0 */
}

static int sim_setup(const struct device *dev, uint8_t options)
{
    ARG_UNUSED(dev);
    if ((options & ~(WDT_OPT_PAUSE_IN_SLEEP | WDT_OPT_PAUSE_HALTED_BY_DBG)) != 0) {
        return -ENOTSUP;
    }
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    int result = 0;
    if (!installed) {
        result = -EINVAL;
    } else if (running) {
        result = -EBUSY;
    } else {
        running = true;
    }
    uint32_t period = timeout_ms;
    k_spin_unlock(&state_lock, key);
    if (result == 0 && arm(period) < 0) {
        return -EIO;
    }
    return result;
}

static int sim_feed(const struct device *dev, int channel)
{
    ARG_UNUSED(dev);
    if (channel != 0) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    bool active = running;
    uint32_t period = timeout_ms;
    k_spin_unlock(&state_lock, key);
    if (!active) {
        return -EINVAL;
    }
    return arm(period) < 0 ? -EIO : 0;
}

static int sim_init(const struct device *dev)
{
    ARG_UNUSED(dev);
    k_work_init_delayable(&expiry_work, expired);
    return 0;
}

/* The watchdog cannot be disabled once started, as on the board: System never stops it. */
static int sim_disable(const struct device *dev)
{
    ARG_UNUSED(dev);
    return -EPERM;
}

static DEVICE_API(wdt, sim_api) = {.install_timeout = sim_install_timeout,
                                   .setup = sim_setup,
                                   .disable = sim_disable,
                                   .feed = sim_feed};
DEVICE_DEFINE(crux_system_sim_wdt, "crux_system_sim_wdt", sim_init, NULL, NULL, NULL, POST_KERNEL,
              CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &sim_api);

const struct device *crux_system_sim_watchdog_device(void)
{
    return DEVICE_GET(crux_system_sim_wdt);
}

bool crux_system_sim_watchdog_running(void)
{
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    bool active = running;
    k_spin_unlock(&state_lock, key);
    return active;
}
