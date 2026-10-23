/* SPDX-License-Identifier: Apache-2.0 */
#ifndef NVS_PROBE_RECORDS_H
#define NVS_PROBE_RECORDS_H
#include <stddef.h>
#include <stdint.h>
/* Test-only nonnull 8-byte output; no platform API. */
size_t probe_record(unsigned index, unsigned revision, uint8_t wire[8]);
#endif
