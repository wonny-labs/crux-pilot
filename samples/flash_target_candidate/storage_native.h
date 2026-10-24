/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CRUX_CANDIDATE_STORAGE_NATIVE_H
#define CRUX_CANDIDATE_STORAGE_NATIVE_H
#include <stdbool.h>
#include <stdint.h>
struct flash_area;
int crux_candidate_area_open(uint8_t id, const struct flash_area **area);
/* Retained compile-only composition, never invoked by main. Provisioning is an
 * explicit destructive mode, never inferred from mount/read errors. */
int crux_candidate_fixed_trial(bool provision);
#endif
