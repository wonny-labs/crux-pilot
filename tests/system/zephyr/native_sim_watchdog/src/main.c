#include <crux/status.h>
#include <crux/system.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* ADR-0005 route A, seen as a consumer would see it: public System calls only, the simulated
 * watchdog selected by CONFIG_CRUX_SYSTEM_NATIVE_SIM_WATCHDOG. A simulated watchdog is not
 * hardware evidence.
 *
 * Boot 1 (cause POR): read and clear the cause, start the watchdog, feed it past one timeout, then
 * stop feeding. The simulated device restarts the process (CONFIG_NATIVE_SIM_REBOOT).
 * Boot 2 (cause SOFTWARE, never WATCHDOG on native_sim): tell it apart from boot 1, find a fresh
 * state, start the watchdog again and keep feeding, or the restart loop would never end. */

#define TIMEOUT_MS 2000
#define FEED_PERIOD_MS 500
#define FEEDS 6

static void fail(const char *what) FUNC_NORETURN;
static void fail(const char *what)
{
    printk("simwdt: FAIL %s\n", what);
    /* A fatal error ends the run at once, so a failure is not mistaken for a timeout. */
    k_panic();
    for (;;) {
    }
}

static void expect(bool condition, const char *what)
{
    if (!condition) {
        fail(what);
    }
}

static void expect_state(crux_watchdog_state_t state, const char *what)
{
    crux_system_status_t status;
    expect(crux_system_status_get(&status) == CRUX_OK, "status get");
    expect(status.watchdog_emulated, "watchdog_emulated");
    expect(status.watchdog_state == state, what);
    expect(state != CRUX_WATCHDOG_ACTIVE ||
               (status.watchdog_status == CRUX_OK && status.watchdog_timeout_ms == TIMEOUT_MS),
           "active status and timeout");
}

static uint32_t read_and_clear_cause(void)
{
    crux_reset_cause_t cause;
    expect(crux_system_reset_cause_get(&cause) == CRUX_OK, "cause get");
    expect((cause.causes & CRUX_RESET_CAUSE_WATCHDOG) == 0,
           "WATCHDOG bit unsupported on native_sim");
    expect((cause.supported & CRUX_RESET_CAUSE_WATCHDOG) == 0, "WATCHDOG bit not supported");
    expect(crux_system_reset_cause_clear() == CRUX_OK, "cause clear");
    return cause.causes;
}

static void start(crux_watchdog_t *token)
{
    expect(crux_watchdog_start(TIMEOUT_MS, token) == CRUX_OK, "start");
    expect_state(CRUX_WATCHDOG_ACTIVE, "state active");
}

int main(void)
{
    crux_watchdog_t token = {0};
    uint32_t cause = read_and_clear_cause();

    if (cause == CRUX_RESET_CAUSE_SOFTWARE) {
        printk("simwdt: boot 2 cause SOFTWARE, not WATCHDOG\n");
        expect_state(CRUX_WATCHDOG_IDLE, "boot 2 fresh state");
        printk("simwdt: boot 2 fresh state, idle, simulated\n");
        start(&token);
        for (int i = 0; i < FEEDS; ++i) {
            k_msleep(FEED_PERIOD_MS);
            expect(crux_watchdog_feed(token) == CRUX_OK, "boot 2 feed");
        }
        expect_state(CRUX_WATCHDOG_ACTIVE, "boot 2 active after feed cycle");
        printk("simwdt: boot 2 feed cycle ok, no second restart, scenario complete\n");
        for (;;) {
            k_msleep(FEED_PERIOD_MS);
            expect(crux_watchdog_feed(token) == CRUX_OK, "boot 2 feed");
        }
    }

    expect(cause == CRUX_RESET_CAUSE_POR, "boot 1 cause is POR");
    printk("simwdt: boot 1 cause POR\n");
    expect_state(CRUX_WATCHDOG_IDLE, "boot 1 idle");
    printk("simwdt: idle before start, simulated\n");
    start(&token);
    printk("simwdt: started, %d ms, active, simulated\n", TIMEOUT_MS);
    for (int i = 0; i < FEEDS; ++i) {
        k_msleep(FEED_PERIOD_MS);
        expect(crux_watchdog_feed(token) == CRUX_OK, "boot 1 feed");
    }
    expect_state(CRUX_WATCHDOG_ACTIVE, "active after feed cycle");
    printk("simwdt: feed cycle ok, %d feeds over %d ms, no restart\n", FEEDS,
           FEEDS * FEED_PERIOD_MS);
    printk("simwdt: missed feed, no more feeds\n");
    k_sleep(K_FOREVER);
    return 0;
}
