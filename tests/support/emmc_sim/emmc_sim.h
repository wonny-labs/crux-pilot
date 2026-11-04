/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Control interface of the test-only eMMC simulator. It is a model written from the needs of the
 * Zephyr MMC stack, so results show how the stack behaves against it and not how a device behaves.
 */
#ifndef CRUX_TESTS_EMMC_SIM_H
#define CRUX_TESTS_EMMC_SIM_H

#include <stdbool.h>
#include <stdint.h>

enum emmc_sim_op { EMMC_SIM_OP_READ, EMMC_SIM_OP_WRITE, EMMC_SIM_OP_FLUSH };

/* Fail the nth operation (1-based) for `attempts` consecutive tries; UINT32_MAX is a hard fault. */
void emmc_sim_fail(enum emmc_sim_op op, uint32_t nth, uint32_t attempts);
void emmc_sim_fail_command(uint8_t opcode, uint32_t nth, uint32_t attempts);
void emmc_sim_reject_next_switch(void);         /* next CMD6 sets status bit 7 */
void emmc_sim_status_not_ready(uint32_t count); /* next `count` CMD13 report not ready */
void emmc_sim_hold_busy(bool busy);             /* sdhc_card_busy reports busy now */
/* Armed now, taking effect only after the next CMD6, so that a wait before the flush does not use
 * them up. Bit 7 from a rejected switch appears in the first CMD13 after that CMD6.
 * Without `forever`, the card is busy for one sdhc_card_busy poll. */
void emmc_sim_busy_after_switch(bool forever);
void emmc_sim_not_ready_after_switch(uint32_t count);
void emmc_sim_clear_faults(void);
void emmc_sim_set_cache_kb(uint32_t kb); /* takes effect at the next card init */

/* Each counter is requests received, one per sdhc_request whatever its outcome. */
struct emmc_sim_stats {
    uint32_t reads, writes, flush_commands, status_reads;
    uint32_t misaligned_transfers; /* data buffers not aligned to CONFIG_SDHC_BUFFER_ALIGNMENT */
    bool cache_on;
};

void emmc_sim_get_stats(struct emmc_sim_stats *out);
void emmc_sim_reset_stats(void);

/* What would survive a power cut, which leaves out data still held in the cache. */
int emmc_sim_read_persistent(uint32_t sector, void *out, uint32_t count);

/* Adds a mark to the trace given with --emmc-trace. */
void emmc_sim_trace_mark(uint32_t id);

#endif /* CRUX_TESTS_EMMC_SIM_H */
