#include "network_mdio_trace.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

/* This observer adds no MDIO command, register write or background execution. */
void crux_m3_mdio_trace_observe(void) {
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
