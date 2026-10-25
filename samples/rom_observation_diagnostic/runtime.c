/* SPDX-License-Identifier: Apache-2.0 */
#include "observation.h"
#include <errno.h>
#include <fsl_clock.h>
#include <fsl_gpt.h>
#define CRUX_OBSERVATION_GATES (CCM_CCGR0_CG12(3) | CCM_CCGR0_CG13(3))

int crux_observation_capture_runtime(void) {
    /* Reviewed plain status/config registers only. No FIFO, interrupt-status
     * clear, write-one-to-clear, POP, watchdog feed/disable or clock writes. */
    crux_observation_result.runtime_ccr = SCB->CCR;
    crux_observation_result.runtime_mpu = MPU->CTRL;
    crux_observation_result.runtime_wdog[0] = WDOG1->WCR;
    crux_observation_result.runtime_wdog[1] = WDOG1->WMCR;
    crux_observation_result.runtime_wdog[2] = WDOG2->WCR;
    crux_observation_result.runtime_wdog[3] = WDOG2->WMCR;
    crux_observation_result.runtime_wdog[4] = RTWDOG->CS;
    crux_observation_result.runtime_control = __get_CONTROL();
    crux_observation_result.runtime_ipsr = __get_IPSR();
    crux_observation_result.runtime_primask = __get_PRIMASK();
    crux_observation_result.runtime_cpacr = SCB->CPACR;
    crux_observation_result.runtime_fpccr = FPU->FPCCR;
    struct crux_rom_profile profile = {0};
    bool profile_supported = crux_rom_read_capture_supported(&profile);
    crux_observation_result.runtime_profile = profile;
    crux_observation_result.ccgr0 = CCM->CCGR0;
    crux_observation_result.xtal_lowpwr = XTALOSC24M->LOWPWR_CTRL;
    crux_observation_result.xtal_misc0 = XTALOSC24M->MISC0;
    crux_observation_result.nominal_osc_hz = CLOCK_GetOscFreq();
    /* A gated peripheral is unavailable, not implicitly enabled or read. */
    if ((crux_observation_result.ccgr0 & CRUX_OBSERVATION_GATES) == CRUX_OBSERVATION_GATES) {
        crux_observation_result.timer_available = 1;
        crux_observation_result.gpt2_before[0] = GPT2->CR;
        crux_observation_result.gpt2_before[1] = GPT2->PR;
        crux_observation_result.gpt2_before[2] = GPT2->IR;
        crux_observation_result.gpt2_before[3] = GPT2->SR;
        crux_observation_result.gpt2_before[4] = GPT2->CNT;
    }
    if (!profile_supported || !crux_observation_result.timer_available ||
        (crux_observation_result.runtime_ccr & 0x10000U) ||
        (crux_observation_result.runtime_mpu & 1U) ||
        ((crux_observation_result.runtime_wdog[0] | crux_observation_result.runtime_wdog[2]) &
         4U) ||
        ((crux_observation_result.runtime_wdog[1] | crux_observation_result.runtime_wdog[3]) &
         1U) ||
        (crux_observation_result.runtime_wdog[4] & 0x80U) ||
        (crux_observation_result.runtime_control & 5U) || crux_observation_result.runtime_ipsr ||
        (crux_observation_result.runtime_cpacr & 0xf00000U) ||
        (crux_observation_result.runtime_fpccr & 1U)) {
        return -EACCES;
    }
    return 0;
}
void crux_observation_capture_timer_after(void) {
    /* GPT2 intentionally remains running with no IRQ/output. These are raw
     * post-return observations, not a measured oscillator or stopped timer. */
    crux_observation_result.gpt2_after[0] = GPT2->CR;
    crux_observation_result.gpt2_after[1] = GPT2->PR;
    crux_observation_result.gpt2_after[2] = GPT2->IR;
    crux_observation_result.gpt2_after[3] = GPT2->SR;
    crux_observation_result.gpt2_after[4] = GPT2->CNT;
}
