#include "network_mdio_trace.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#ifdef CONFIG_CRUX_M3_DEFERRED_OUTPUT
#include "native_logger.h"
#include <zephyr/logging/log_ctrl.h>
#endif

/* This observer adds no MDIO command, register write or background execution. */
void crux_m3_mdio_trace_observe(void)
{
    static bool reported;
    const struct device *mdio = DEVICE_DT_GET(DT_PARENT(DT_NODELABEL(phy)));
    const struct device *phy = DEVICE_DT_GET(DT_NODELABEL(phy));
    if (reported) {
        return;
    }
    struct crux_m3_mdio_failure record;
    if (!crux_m3_mdio_failure_get(mdio, &record)) {
        return;
    }
    /* The application emits after the copy releases IRQ protection; it owns no MDIO mutex. */
    printk("M3_MDIO_FIRST_FAILURE monotonic_ms=%u started_cycles=%u elapsed_cycles=%u "
           "clock_hz=%u wait_interrupt=%u phy=%u reg=%u op=%u result=%d eir=%u mmfr=%u "
           "eimr=%u ecr=%u mscr=%u sem_count=%u valid=%u mdio_ready=%u phy_ready=%u\n",
           record.monotonic_ms, record.started_cycles, record.elapsed_cycles, record.clock_hz,
           record.wait_interrupt, record.phy, record.reg, record.op, record.result, record.eir,
           record.mmfr, record.eimr, record.ecr, record.mscr, record.sem_count, record.valid,
           (unsigned int)device_is_ready(mdio), (unsigned int)device_is_ready(phy));
    reported = true;
}

#ifdef CONFIG_CRUX_M3_DEFERRED_OUTPUT
static bool output_started;
static bool output_final_reported;
static int64_t output_started_ms;

void crux_m3_deferred_start(void)
{
    if (output_started) {
        return;
    }
    output_started_ms = k_uptime_get();
    output_started = true;
    printk("M3_OUTPUT_START monotonic_ms=%lld window_ms=15000\n", (long long)output_started_ms);
}

static void report_logger(bool final)
{
    struct m3_logger_resources logger = m3_logger_resources_get();
    struct crux_m3_mdio_failure record;
    const struct device *mdio = DEVICE_DT_GET(DT_PARENT(DT_NODELABEL(phy)));
    int64_t before = k_uptime_get();
    bool valid = crux_m3_mdio_failure_get(mdio, &record);
    int64_t after = k_uptime_get();
    /* No-failure meaning ends at this getter snapshot; later failures remain possible. */
    printk("%s snapshot_before_ms=%lld snapshot_after_ms=%lld elapsed_ms=%lld "
           "failure_valid=%u logger_found=%u status=%d configured=%u usable=%zu "
           "unused=%zu priority=%d queued=%u\n",
           final ? "M3_OUTPUT_FINAL" : "M3_OUTPUT", (long long)before, (long long)after,
           (long long)(before - output_started_ms), (unsigned int)valid, logger.found,
           logger.status, (unsigned int)CONFIG_LOG_PROCESS_THREAD_STACK_SIZE, logger.usable,
           logger.unused, logger.priority, (unsigned int)log_buffered_cnt());
}

void crux_m3_deferred_report(void)
{
    report_logger(false);
}

void crux_m3_deferred_poll(void)
{
    if (output_started && !output_final_reported && k_uptime_get() - output_started_ms >= 15000) {
        output_final_reported = true;
        crux_m3_mdio_trace_observe();
        report_logger(true);
    }
}
#endif
