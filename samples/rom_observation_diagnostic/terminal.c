/* SPDX-License-Identifier: Apache-2.0 */
#include "flash_poc.h"
#include "observation.h"
_Noreturn __attribute__((noinline)) void crux_flash_poc_terminal(int reason) {
    crux_observation_result.result = reason;
    crux_observation_result.disposition = CRUX_OBSERVATION_RAM_CONTAINED;
    crux_observation_result.phase = CRUX_OBSERVATION_CONTAINED;
    for (;;) {
        __asm__ volatile("nop");
    }
}
