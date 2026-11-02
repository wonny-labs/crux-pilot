/* Physical item P1 of docs/physical-verification-plan.md: GPIO input routing and polarity.
 * Reads the Crux GPIO line bound to the board user button every sample period and prints the
 * logical value with the target's monotonic uptime. The operator log is the second witness. */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "consumer.h"

#define SAMPLE_PERIOD_MS 100

int main(void)
{
    printk("P1 gpio-input build=%s board=%s period_ms=%d\n", CRUX_P1_BUILD_ID, CONFIG_BOARD,
           SAMPLE_PERIOD_MS);
    for (;;) {
        bool active = false;
        crux_status_t status = read_logical_input(&active);
        printk("P1 t_ms=%lld status=%d active=%d\n", k_uptime_get(), (int)status,
               status == CRUX_OK ? (int)active : -1);
        k_msleep(SAMPLE_PERIOD_MS);
    }
    return 0;
}
