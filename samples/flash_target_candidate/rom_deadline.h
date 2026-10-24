/* SPDX-License-Identifier: Apache-2.0 */
/* Sample-private software experiment; never target elapsed qualification. */
#ifndef CRUX_ROM_DEADLINE_H
#define CRUX_ROM_DEADLINE_H
#include <stdbool.h>
#include <stdint.h>
/* No startup caller. Only controlled software currently invokes configuration. */
int crux_rom_gpt2_candidate_init(void);
/* Caller explicitly declares test ticks, nominally 24 MHz, not NOR maxima. */
int crux_rom_candidate_deadline_begin(uint64_t declared_test_ticks);
/* After positive NOR-ready + controller-idle only; faults are RAM-terminal.
 * Healthy finish is required before another begin; faults cannot be reset. */
void crux_rom_candidate_deadline_finish(void);
bool crux_rom_candidate_deadline_expired(void);
void crux_rom_candidate_wait_check(void);
#endif
