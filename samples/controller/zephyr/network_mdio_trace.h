#ifndef CRUX_SAMPLE_NETWORK_MDIO_TRACE_H
#define CRUX_SAMPLE_NETWORK_MDIO_TRACE_H

#include <stdbool.h>
#include <stdint.h>

struct device;

/* Private diagnostic data, copied in the CPU context rather than debug RAM reads. */
struct crux_m3_mdio_failure {
    uint32_t monotonic_ms;
    uint32_t started_cycles;
    uint32_t elapsed_cycles;
    uint32_t clock_hz;
    uint32_t wait_interrupt;
    uint32_t phy;
    uint32_t reg;
    uint32_t op;
    int32_t result;
    uint32_t eir;
    uint32_t mmfr;
    uint32_t eimr;
    uint32_t ecr;
    uint32_t mscr;
    uint32_t sem_count;
    uint32_t valid;
};

bool crux_m3_mdio_failure_get(const struct device *dev, struct crux_m3_mdio_failure *out);
void crux_m3_mdio_trace_observe(void);
#ifdef CONFIG_CRUX_M3_DEFERRED_OUTPUT
void crux_m3_deferred_start(void);
void crux_m3_deferred_poll(void);
void crux_m3_deferred_report(void);
#endif

#endif
