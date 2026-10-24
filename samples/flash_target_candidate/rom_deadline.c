/* SPDX-License-Identifier: Apache-2.0 */
#include "rom_deadline.h"
#include "flash_poc.h"
#include <errno.h>
#include <fsl_gpt.h>

#define CRUX_GPT2_GATES (CCM_CCGR0_CG12(3) | CCM_CCGR0_CG13(3))
#define CRUX_GPT2_RUNNING                                                                          \
    (GPT_CR_FRR_MASK | GPT_CR_ENMOD_MASK | GPT_CR_CLKSRC(kGPT_ClockSource_Osc) |                   \
     GPT_CR_EN_24M_MASK | GPT_CR_EN_MASK)
/* Nominal firmware oscillator declaration, not measured physical frequency. */
#define CRUX_GPT2_NOMINAL_HZ 24000000U
/* Observation fallback only. This is not an elapsed duration. */
#define CRUX_GPT2_OBSERVATIONS 1024U
static struct {
    uint32_t source, start, previous, ticks, unchanged;
    bool initialized, active, fault;
} crux_rom_deadline;

static bool crux_rom_gpt2_source_matches(void) {
    return (CCM->CCGR0 & CRUX_GPT2_GATES) == CRUX_GPT2_GATES &&
           XTALOSC24M->LOWPWR_CTRL == crux_rom_deadline.source &&
           (crux_rom_deadline.source & XTALOSC24M_LOWPWR_CTRL_OSC_SEL_MASK) == 0 &&
           (crux_rom_deadline.source & XTALOSC24M_LOWPWR_CTRL_XTALOSC_PWRUP_STAT_MASK) != 0 &&
           (XTALOSC24M->MISC0 & XTALOSC24M_MISC0_OSC_XTALOK_MASK) != 0;
}
static bool crux_rom_gpt2_running(void) {
    return crux_rom_gpt2_source_matches() && GPT2->CR == CRUX_GPT2_RUNNING && GPT2->PR == 0 &&
           GPT2->IR == 0;
}
int crux_rom_gpt2_candidate_init(void) {
    /* No automatic caller. Already-enabled gates are required for register
     * observation; this experiment never changes shared CCM/oscillator state.
     * Zero stopped registers are necessary, not proof of physical ownership. */
    if (crux_rom_deadline.fault) {
        return -EACCES;
    }
    crux_rom_deadline.fault = true;
    crux_rom_deadline.active = false;
    if (crux_rom_deadline.initialized || (CCM->CCGR0 & CRUX_GPT2_GATES) != CRUX_GPT2_GATES ||
        (XTALOSC24M->LOWPWR_CTRL & XTALOSC24M_LOWPWR_CTRL_OSC_SEL_MASK) != 0 ||
        (XTALOSC24M->LOWPWR_CTRL & XTALOSC24M_LOWPWR_CTRL_XTALOSC_PWRUP_STAT_MASK) == 0 ||
        (XTALOSC24M->MISC0 & XTALOSC24M_MISC0_OSC_XTALOK_MASK) == 0 ||
        CLOCK_GetOscFreq() != CRUX_GPT2_NOMINAL_HZ || GPT2->CR != 0 || GPT2->IR != 0 ||
        GPT2->PR != 0 || GPT2->SR != 0 || GPT_GetCurrentTimerCount(GPT2) != 0) {
        return -EACCES;
    }
    crux_rom_deadline.source = XTALOSC24M->LOWPWR_CTRL;
    if (!crux_rom_gpt2_source_matches()) {
        return -EACCES;
    }
    /* GPT peripheral reset only, never NOR/controller/system reset. The SDK's
     * GPT_SoftwareReset spins indefinitely; use the same SWR with a bounded
     * observation refusal here, before any busy-NOR context. */
    GPT2->CR = GPT_CR_SWR_MASK;
    unsigned remaining = CRUX_GPT2_OBSERVATIONS;
    while ((GPT2->CR & GPT_CR_SWR_MASK) != 0) {
        if (remaining-- == 0 || !crux_rom_gpt2_source_matches()) {
            return -ETIMEDOUT;
        }
    }
    if (!crux_rom_gpt2_source_matches() || GPT2->CR || GPT2->IR || GPT2->PR ||
        GPT_GetCurrentTimerCount(GPT2)) {
        return -EACCES;
    }
    /* Explicit OSC source, free run, enable resets count, both dividers 1.
     * No wait/stop/debug/doze mode, interrupts or output compare enabled. */
    GPT2->PR = 0;
    GPT2->CR = CRUX_GPT2_RUNNING & ~GPT_CR_EN_MASK;
    if (GPT2->CR != (CRUX_GPT2_RUNNING & ~GPT_CR_EN_MASK) || GPT2->PR || GPT2->IR) {
        return -EACCES;
    }
    GPT_StartTimer(GPT2);
    uint32_t initial = GPT_GetCurrentTimerCount(GPT2);
    for (unsigned i = 0; i < CRUX_GPT2_OBSERVATIONS; ++i) {
        if (!crux_rom_gpt2_running()) {
            return -EACCES;
        }
        uint32_t delta = GPT_GetCurrentTimerCount(GPT2) - initial;
        if (delta && delta < 0x80000000U) {
            crux_rom_deadline.initialized = true;
            crux_rom_deadline.fault = false;
            return 0;
        }
    }
    return -ETIMEDOUT;
}
int crux_rom_candidate_deadline_begin(uint64_t declared_test_ticks) {
    /* An unfinished episode can never extend its own deadline, including one
     * whose expiry has not yet been polled. Only explicit healthy finish
     * releases the budget for another bounded software episode. */
    if (crux_rom_deadline.active) {
        crux_rom_deadline.fault = true;
        return -EACCES;
    }
    if (!declared_test_ticks || declared_test_ticks >= 0x80000000ULL) {
        crux_rom_deadline.fault = true;
        return -EINVAL;
    }
    if (!crux_rom_deadline.initialized || crux_rom_deadline.fault || !crux_rom_gpt2_running()) {
        crux_rom_deadline.fault = true;
        return -EACCES;
    }
    crux_rom_deadline.ticks = (uint32_t)declared_test_ticks;
    crux_rom_deadline.start = GPT_GetCurrentTimerCount(GPT2);
    crux_rom_deadline.previous = crux_rom_deadline.start;
    crux_rom_deadline.unchanged = 0;
    crux_rom_deadline.active = true;
    return 0;
}
bool crux_rom_candidate_deadline_expired(void) {
    if (!crux_rom_deadline.active || crux_rom_deadline.fault || !crux_rom_gpt2_running()) {
        crux_rom_deadline.fault = true;
        return true;
    }
    uint32_t now = GPT_GetCurrentTimerCount(GPT2);
    uint32_t step = now - crux_rom_deadline.previous;
    uint32_t elapsed = now - crux_rom_deadline.start;
    crux_rom_deadline.unchanged = step ? 0 : crux_rom_deadline.unchanged + 1;
    crux_rom_deadline.previous = now;
    /* Unsigned rollover is supported within the declared half-range. An
     * apparent backward jump, excessive gap, expiry or no-progress faults. */
    if (step >= 0x80000000U || elapsed >= crux_rom_deadline.ticks ||
        crux_rom_deadline.unchanged >= CRUX_GPT2_OBSERVATIONS) {
        crux_rom_deadline.fault = true;
        return true;
    }
    return false;
}
void crux_rom_candidate_wait_check(void) {
    /* A ready controller/FIFO is insufficient when the timer has stopped.
     * Require new timer progress at each lower boundary, with the predicate's
     * observation fallback. This remains conservative software evidence. */
    uint32_t before = crux_rom_deadline.previous;
    do {
        if (crux_rom_candidate_deadline_expired()) {
            crux_flash_poc_terminal(-ETIMEDOUT);
        }
    } while (crux_rom_deadline.previous == before);
}

void crux_rom_candidate_deadline_finish(void) {
    /* Caller must already have positive NOR-ready and controller-idle evidence.
     * This private lifecycle boundary makes no such media observation itself.
     * No current target caller is admitted. An uncertain/expired episode is
     * terminal and can never be finished/rearmed successfully. */
    crux_rom_candidate_wait_check();
    crux_rom_deadline.active = false;
}
