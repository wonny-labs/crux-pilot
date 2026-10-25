/* SPDX-License-Identifier: Apache-2.0 */
/* Private, versioned RAM readback for this one diagnostic image only. */
#ifndef CRUX_ROM_OBSERVATION_H
#define CRUX_ROM_OBSERVATION_H
#include "rom_profile.h"
#include <stdint.h>
#define CRUX_OBSERVATION_ID_STATUS 1U
#define CRUX_OBSERVATION_TICKS 2400000U
#define CRUX_OBSERVATION_NOMINAL_HZ 24000000U
#define CRUX_OBSERVATION_ID_MATCH 1U
#define CRUX_OBSERVATION_GPT2_ATTEMPTED 2U
#define CRUX_OBSERVATION_GPT2_RUNNING 4U
#define CRUX_OBSERVATION_SLOT_INSTALLED 8U
#define CRUX_OBSERVATION_SLOT_RESTORED 16U
#define CRUX_OBSERVATION_KNOWN_READY 32U
#define CRUX_OBSERVATION_INITIAL_STATUS_VALID 64U
#define CRUX_OBSERVATION_ID_VALID 128U
#define CRUX_OBSERVATION_FINAL_STATUS_VALID 256U

enum crux_observation_phase {
    CRUX_OBSERVATION_BOOT,
    CRUX_OBSERVATION_RUNTIME,
    CRUX_OBSERVATION_ENTERING,
    CRUX_OBSERVATION_RESTORED,
    CRUX_OBSERVATION_REFUSED,
    CRUX_OBSERVATION_CONTAINED,
    CRUX_OBSERVATION_COMPLETE
};
enum crux_observation_disposition {
    CRUX_OBSERVATION_NO_ENTRY,
    CRUX_OBSERVATION_RAM_CONTAINED,
    CRUX_OBSERVATION_RETURNED
};
/* All words are fixed-width. id[] contains three byte values, one per word.
 * Phase COMPLETE is the last publication, after ready/slot/context restoration.
 * RESTORED means LUT/ready/deadline only; caller context is still being restored.
 * Earlier phases are partial evidence, never a completion or qualification. */
struct crux_observation_record {
    uint32_t magic, version, size, phase;
    int32_t result;
    uint32_t disposition, flags, declared_ticks, nominal_hz;
    uint32_t pre_c[16];
    uint32_t runtime_ccr, runtime_mpu, runtime_wdog[5];
    uint32_t runtime_control, runtime_ipsr, runtime_primask, runtime_cpacr, runtime_fpccr;
    struct crux_rom_profile runtime_profile;
    uint32_t ccgr0, xtal_lowpwr, xtal_misc0, nominal_osc_hz, timer_available;
    uint32_t gpt2_before[5], gpt2_after[5]; /* CR, PR, IR, SR, CNT; available only with gates */
    uint32_t status_initial, status_final, id[3];
};
extern volatile struct crux_observation_record crux_observation_result;
int crux_observation_run(uint32_t request);
bool crux_rom_read_capture_supported(struct crux_rom_profile *observation);
int crux_observation_capture_runtime(void);
void crux_observation_capture_timer_after(void);
int crux_observation_prepare(void);
#endif
