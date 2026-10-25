/* SPDX-License-Identifier: Apache-2.0 */
#include "observation.h"
#include "island_checks.h"
#include "rom_deadline.h"
#include <errno.h>
#include <stdbool.h>

extern const volatile uint32_t pilot_reset_snapshot[16];
extern int crux_flash_island_prepare(void);
extern int crux_island_enter(void);
volatile struct crux_observation_record crux_observation_result = {
    .magic = 0x4f425356U,
    .version = 1,
    .size = sizeof(struct crux_observation_record),
    .phase = CRUX_OBSERVATION_BOOT,
    .result = -EACCES,
    .declared_ticks = CRUX_OBSERVATION_TICKS,
    .nominal_hz = CRUX_OBSERVATION_NOMINAL_HZ,
};
static bool crux_observation_claimed;

static int crux_observation_refuse(int error) {
    crux_observation_result.result = error;
    crux_observation_result.phase = CRUX_OBSERVATION_REFUSED;
    return error;
}
int crux_observation_run(uint32_t request) {
    /* Exactly one main-thread owner in this fixed composition; no exported
     * service, arbitrary transfer, backup, retry or reinitialization endpoint. */
    if (crux_observation_claimed) {
        return -EALREADY;
    }
    crux_observation_claimed = true;
    if (request != CRUX_OBSERVATION_ID_STATUS) {
        return crux_observation_refuse(-EACCES);
    }
    for (unsigned i = 0; i < 16; ++i) {
        crux_observation_result.pre_c[i] = pilot_reset_snapshot[i];
    }
    crux_observation_result.phase = CRUX_OBSERVATION_RUNTIME;
    if (!crux_island_snapshot_supported(pilot_reset_snapshot)) {
        return crux_observation_refuse(-EACCES);
    }
    int error = crux_observation_capture_runtime();
    if (error) {
        return crux_observation_refuse(error);
    }
    error = crux_flash_island_prepare();
    if (error) {
        return crux_observation_refuse(error);
    }
    error = crux_observation_prepare();
    if (error) {
        return crux_observation_refuse(error);
    }
    /* Already-enabled gates, available crystal and stopped/unowned GPT2 are
     * checked by the actual candidate. No gate/source is silently enabled. */
    crux_observation_result.flags |= CRUX_OBSERVATION_GPT2_ATTEMPTED;
    error = crux_rom_gpt2_candidate_init();
    if (error) {
        return crux_observation_refuse(error);
    }
    crux_observation_result.flags |= CRUX_OBSERVATION_GPT2_RUNNING;
    error = crux_rom_candidate_deadline_begin(CRUX_OBSERVATION_TICKS);
    if (error) {
        return crux_observation_refuse(error);
    }
    crux_observation_result.disposition = CRUX_OBSERVATION_RAM_CONTAINED;
    crux_observation_result.phase = CRUX_OBSERVATION_ENTERING;
    error = crux_island_enter();
    if (error) {
        /* Assembly refused before switching; no NOR command or LUT write. */
        crux_observation_result.disposition = CRUX_OBSERVATION_NO_ENTRY;
        return crux_observation_refuse(error);
    }
    if (crux_observation_result.phase == CRUX_OBSERVATION_REFUSED) {
        crux_observation_result.disposition = CRUX_OBSERVATION_RETURNED;
        return crux_observation_result.result;
    }
    if (crux_observation_result.phase != CRUX_OBSERVATION_RESTORED) {
        return crux_observation_refuse(-EACCES);
    }
    crux_observation_capture_timer_after();
    crux_observation_result.disposition = CRUX_OBSERVATION_RETURNED;
    crux_observation_result.result = 0;
    crux_observation_result.phase = CRUX_OBSERVATION_COMPLETE;
    return 0;
}
