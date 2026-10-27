#include "system_zephyr.h"
#include "interrupt.h"
#include "system_port.h"
#include <errno.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>

K_MUTEX_DEFINE(system_mutex);

bool system_port_context_allowed(void)
{
    return !crux_is_in_isr() && !k_is_pre_kernel();
}

crux_status_t system_port_lock(void)
{
    return k_mutex_lock(&system_mutex, K_FOREVER) == 0 ? CRUX_OK : CRUX_ERR_IO;
}

void system_port_unlock(void)
{
    (void)k_mutex_unlock(&system_mutex);
}

bool system_port_watchdog_emulated(void)
{
    return false;
}

crux_status_t system_port_watchdog_validate(uint32_t timeout_ms)
{
    if (timeout_ms < 500 || timeout_ms > 64000)
        return CRUX_ERR_OUT_OF_RANGE;
    return timeout_ms % 500 == 0 ? CRUX_OK : CRUX_ERR_NOT_SUPPORTED;
}

static crux_status_t watchdog_ready(const struct device *device)
{
    if (device == NULL)
        return CRUX_ERR_NOT_SUPPORTED;
    return device_is_ready(device) ? CRUX_OK : CRUX_ERR_NOT_READY;
}

crux_status_t system_port_watchdog_probe(system_watchdog_probe_t *out)
{
    crux_status_t result = watchdog_ready(system_zephyr_watchdog_device());
    if (result == CRUX_OK)
        *out = (system_watchdog_probe_t){system_zephyr_watchdog_running(), false};
    return result;
}

crux_status_t system_port_watchdog_start(uint32_t timeout_ms)
{
    const struct device *device = system_zephyr_watchdog_device();
    crux_status_t result = watchdog_ready(device);
    if (result != CRUX_OK)
        return result;
    const struct wdt_timeout_cfg cfg = {
        .window = {.min = 0, .max = timeout_ms}, .callback = NULL, .flags = WDT_FLAG_RESET_SOC};
    /* Pinned RT1060 driver has exactly channel zero. Any other return is
     * uncertain installation; common code quarantines, never disables/retries. */
    if (wdt_install_timeout(device, &cfg) != 0)
        return CRUX_ERR_IO;
    return wdt_setup(device, WDT_OPT_PAUSE_HALTED_BY_DBG) == 0 ? CRUX_OK : CRUX_ERR_IO;
}

crux_status_t system_port_watchdog_feed(void)
{
    const struct device *device = system_zephyr_watchdog_device();
    crux_status_t result = watchdog_ready(device);
    if (result != CRUX_OK)
        return result;
    return wdt_feed(device, 0) == 0 ? CRUX_OK : CRUX_ERR_IO;
}

crux_status_t system_port_reset_status(void)
{
    return CRUX_OK;
}

crux_status_t system_port_reset(void)
{
    sys_reboot(SYS_REBOOT_COLD);
    /* Native declaration is nonreturning: no fabricated success result. */
}

bool system_port_reset_cause_supported(void)
{
    return true;
}

static crux_status_t hwinfo_result(int result)
{
    if (result == 0)
        return CRUX_OK;
    return result == -ENOSYS || result == -ENOTSUP ? CRUX_ERR_NOT_SUPPORTED : CRUX_ERR_IO;
}

static uint32_t normalize(uint32_t raw)
{
    const struct {
        uint32_t native;
        uint32_t portable;
    } bits[] = {{RESET_POR, CRUX_RESET_CAUSE_POR},
                {RESET_PIN, CRUX_RESET_CAUSE_PIN},
                {RESET_SOFTWARE, CRUX_RESET_CAUSE_SOFTWARE},
                {RESET_WATCHDOG, CRUX_RESET_CAUSE_WATCHDOG},
                {RESET_DEBUG, CRUX_RESET_CAUSE_DEBUG},
                {RESET_CPU_LOCKUP, CRUX_RESET_CAUSE_CPU_LOCKUP},
                {RESET_HARDWARE, CRUX_RESET_CAUSE_HARDWARE},
                {RESET_SECURITY, CRUX_RESET_CAUSE_SECURITY},
                {RESET_USER, CRUX_RESET_CAUSE_USER},
                {RESET_TEMPERATURE, CRUX_RESET_CAUSE_TEMPERATURE}};
    uint32_t normalized = 0, known = 0;
    for (size_t i = 0; i < ARRAY_SIZE(bits); ++i) {
        known |= bits[i].native;
        if ((raw & bits[i].native) != 0)
            normalized |= bits[i].portable;
    }
    if ((raw & ~known) != 0)
        normalized |= CRUX_RESET_CAUSE_OTHER;
    return normalized;
}

crux_status_t system_port_reset_cause_get(crux_reset_cause_t *out)
{
    uint32_t supported, causes;
    crux_status_t result = hwinfo_result(hwinfo_get_supported_reset_cause(&supported));
    if (result != CRUX_OK)
        return result;
    result = hwinfo_result(hwinfo_get_reset_cause(&causes));
    if (result != CRUX_OK)
        return result;
    if ((causes & ~supported) != 0)
        return CRUX_ERR_IO;
    *out = (crux_reset_cause_t){normalize(supported), normalize(causes)};
    return CRUX_OK;
}

crux_status_t system_port_reset_cause_clear(void)
{
    return hwinfo_result(hwinfo_clear_reset_cause());
}
