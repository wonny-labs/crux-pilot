#include "system_port.h"
#include "system_zephyr.h"
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/irq_offload.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/ztest.h>

/* Real Zephyr driver dispatch and mutexes; only physical effects are doubled. */
static bool running, ready = true;
static int install_result, setup_result, feed_result;
static unsigned installs, setups, feeds, disables, clears;
static struct wdt_timeout_cfg installed;
static uint8_t setup_options;
static int fixture_init(const struct device *dev)
{
    ARG_UNUSED(dev);
    return 0;
}
static int unready_init(const struct device *dev)
{
    ARG_UNUSED(dev);
    return -ENODEV;
}
static int install(const struct device *dev, const struct wdt_timeout_cfg *cfg)
{
    ARG_UNUSED(dev);
    ++installs;
    installed = *cfg;
    return install_result;
}
static int setup(const struct device *dev, uint8_t options)
{
    ARG_UNUSED(dev);
    ++setups;
    setup_options = options;
    if (setup_result == 0)
        running = true;
    return setup_result;
}
static int feed(const struct device *dev, int channel)
{
    ARG_UNUSED(dev);
    zassert_equal(channel, 0);
    ++feeds;
    return feed_result;
}
static int disable(const struct device *dev)
{
    ARG_UNUSED(dev);
    ++disables;
    return 0;
}
static DEVICE_API(wdt, fixture_api) = {
    .install_timeout = install, .setup = setup, .feed = feed, .disable = disable};
DEVICE_DEFINE(test_wdt, "test_wdt", fixture_init, NULL, NULL, NULL, POST_KERNEL, 50, &fixture_api);
DEVICE_DEFINE(unready_wdt, "unready_wdt", unready_init, NULL, NULL, NULL, POST_KERNEL, 50,
              &fixture_api);
const struct device *__wrap_system_zephyr_watchdog_device(void)
{
    return ready ? DEVICE_GET(test_wdt) : DEVICE_GET(unready_wdt);
}
bool __wrap_system_zephyr_watchdog_running(void)
{
    return running;
}
static bool override_cause;
static uint32_t supported, cause;
static int cause_error, support_error, clear_error;
int __real_z_impl_hwinfo_get_reset_cause(uint32_t *out);
int __real_z_impl_hwinfo_get_supported_reset_cause(uint32_t *out);
int __real_z_impl_hwinfo_clear_reset_cause(void);
int __wrap_z_impl_hwinfo_get_reset_cause(uint32_t *out)
{
    if (cause_error)
        return cause_error;
    if (!override_cause)
        return __real_z_impl_hwinfo_get_reset_cause(out);
    *out = cause;
    return 0;
}
int __wrap_z_impl_hwinfo_get_supported_reset_cause(uint32_t *out)
{
    if (support_error)
        return support_error;
    if (!override_cause)
        return __real_z_impl_hwinfo_get_supported_reset_cause(out);
    *out = supported;
    return 0;
}
int __wrap_z_impl_hwinfo_clear_reset_cause(void)
{
    ++clears;
    if (clear_error)
        return clear_error;
    return __real_z_impl_hwinfo_clear_reset_cause();
}
K_SEM_DEFINE(reset_seen, 0, 1);
K_SEM_DEFINE(reset_release, 0, 1);
static int reboot_type;
FUNC_NORETURN void __wrap_sys_reboot(int type)
{
    reboot_type = type;
    k_sem_give(&reset_seen);
    (void)k_sem_take(&reset_release, K_FOREVER);
    k_thread_abort(k_current_get());
    for (;;)
        k_sleep(K_FOREVER);
}
K_THREAD_STACK_DEFINE(reset_stack, 2048);
static struct k_thread reset_thread;
static void reset_entry(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    (void)crux_system_reset();
    zassert_unreachable("native reset unexpectedly returned");
}
static void irq_check(const void *unused)
{
    ARG_UNUSED(unused);
    crux_system_status_t status;
    crux_reset_cause_t report;
    crux_watchdog_t token = {0};
    zassert_equal(crux_system_status_get(&status), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_system_reset_cause_get(&report), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_system_reset_cause_clear(), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_system_reset(), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_watchdog_start(2000, &token), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_watchdog_feed(token), CRUX_ERR_NOT_SUPPORTED);
}
ZTEST(system, test_private_native_mappings)
{
    installs = setups = feeds = 0;
    ready = true;
    running = false;
    system_watchdog_probe_t probe;
    zassert_equal(crux_watchdog_validate_timeout(499), CRUX_ERR_OUT_OF_RANGE);
    zassert_equal(crux_watchdog_validate_timeout(750), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_watchdog_validate_timeout(64000), CRUX_OK);
    zassert_equal(crux_watchdog_probe(&probe), CRUX_OK);
    zassert_false(probe.running);
    install_result = -ENOMEM;
    zassert_equal(crux_watchdog_impl_start(2000), CRUX_ERR_IO);
    zassert_equal(setups, 0);
    install_result = 1;
    zassert_equal(crux_watchdog_impl_start(2000), CRUX_ERR_IO);
    zassert_equal(setups, 0);
    install_result = 0;
    setup_result = -EIO;
    zassert_equal(crux_watchdog_impl_start(2000), CRUX_ERR_IO);
    setup_result = 0;
    zassert_equal(crux_watchdog_impl_start(2000), CRUX_OK);
    zassert_equal(installed.window.min, 0);
    zassert_equal(installed.window.max, 2000);
    zassert_is_null(installed.callback);
    zassert_equal(installed.flags, WDT_FLAG_RESET_SOC);
    zassert_equal(setup_options, WDT_OPT_PAUSE_HALTED_BY_DBG);
    zassert_equal(crux_watchdog_probe(&probe), CRUX_OK);
    zassert_true(probe.running);
    zassert_false(probe.expired);
    feed_result = -EAGAIN;
    zassert_equal(crux_watchdog_impl_feed(), CRUX_ERR_IO);
    feed_result = 0;
    zassert_equal(crux_watchdog_impl_feed(), CRUX_OK);
    zassert_equal(disables, 0);
    running = false;
}
ZTEST(system, test_public_boot_episode)
{
    installs = setups = feeds = 0;
    install_result = setup_result = feed_result = 0;
    ready = false;
    running = false;
    irq_offload(irq_check, NULL);
    crux_watchdog_t owner = {0};
    zassert_equal(crux_watchdog_start(2000, &owner), CRUX_ERR_NOT_READY);
    zassert_equal(installs, 0);
    ready = true;
    crux_system_status_t status;
    zassert_equal(crux_system_status_get(&status), CRUX_OK);
    zassert_false(status.watchdog_emulated);
    zassert_equal(status.watchdog_state, CRUX_WATCHDOG_IDLE);
    zassert_equal(status.reset_cause_status, CRUX_OK);
    zassert_equal(crux_system_reset_cause_clear(), CRUX_ERR_INVALID_STATE);
    cause_error = -EIO;
    crux_reset_cause_t report = {7, 3};
    zassert_equal(crux_system_reset_cause_get(&report), CRUX_ERR_IO);
    zassert_equal(report.supported, 7);
    zassert_equal(report.causes, 3);
    zassert_equal(crux_system_reset_cause_clear(), CRUX_ERR_INVALID_STATE);
    cause_error = 0;
    override_cause = true;
    supported = RESET_POR;
    cause = RESET_WATCHDOG;
    zassert_equal(crux_system_reset_cause_get(&report), CRUX_ERR_IO);
    supported = RESET_CPU_LOCKUP | RESET_SOFTWARE | BIT(31);
    cause = supported;
    zassert_equal(crux_system_reset_cause_get(&report), CRUX_OK);
    zassert_equal(report.causes,
                  CRUX_RESET_CAUSE_CPU_LOCKUP | CRUX_RESET_CAUSE_SOFTWARE | CRUX_RESET_CAUSE_OTHER);
    support_error = -ENOSYS;
    zassert_equal(crux_system_reset_cause_get(&report), CRUX_ERR_NOT_SUPPORTED);
    support_error = 0;
    override_cause = false;
    /* Real native_sim HWINFO fresh-boot POR, non-destructive until explicit clear. */
    zassert_equal(crux_system_reset_cause_get(&report), CRUX_OK);
    crux_reset_cause_t retained = report;
    zassert_equal(report.causes, CRUX_RESET_CAUSE_POR);
    clear_error = -EIO;
    zassert_equal(crux_system_reset_cause_clear(), CRUX_ERR_IO);
    unsigned before = clears;
    zassert_equal(crux_system_reset_cause_clear(), CRUX_ERR_INVALID_STATE);
    zassert_equal(clears, before);
    clear_error = 0;
    zassert_equal(crux_system_reset_cause_get(&report), CRUX_OK);
    zassert_equal(crux_system_reset_cause_clear(), CRUX_OK);
    zassert_equal(crux_system_reset_cause_get(&report), CRUX_OK);
    zassert_equal(report.causes, 0);
    zassert_equal(retained.causes, CRUX_RESET_CAUSE_POR);
    k_thread_create(&reset_thread, reset_stack, K_THREAD_STACK_SIZEOF(reset_stack), reset_entry,
                    NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    zassert_equal(k_sem_take(&reset_seen, K_SECONDS(1)), 0);
    /* Parent task observes while reset dispatch remains suspended: a recursive
     * mutex re-entry on the reset thread would not prove release. */
    zassert_equal(crux_system_status_get(&status), CRUX_OK);
    k_sem_give(&reset_release);
    zassert_equal(k_thread_join(&reset_thread, K_SECONDS(1)), 0);
    zassert_equal(reboot_type, SYS_REBOOT_COLD);
    zassert_equal(crux_watchdog_start(2000, &owner), CRUX_OK);
    zassert_not_equal(owner.reserved, 0);
    zassert_equal(crux_watchdog_feed(owner), CRUX_OK);
    feed_result = -EIO;
    zassert_equal(crux_watchdog_feed(owner), CRUX_ERR_IO);
    feed_result = 0;
    zassert_equal(crux_watchdog_feed(owner), CRUX_ERR_IO);
    zassert_equal(crux_system_status_get(&status), CRUX_OK);
    zassert_equal(status.watchdog_state, CRUX_WATCHDOG_FAULTED);
    crux_watchdog_t other = {0};
    zassert_equal(crux_watchdog_start(2000, &other), CRUX_ERR_INVALID_STATE);
    zassert_equal(installs, 1);
    zassert_equal(disables, 0);
}
ZTEST_SUITE(system, NULL, NULL, NULL, NULL, NULL);
