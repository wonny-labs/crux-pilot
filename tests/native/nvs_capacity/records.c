/* SPDX-License-Identifier: Apache-2.0 */
#include "records.h"

size_t probe_record(unsigned index, unsigned revision, uint8_t wire[8])
{
    wire[0] = 1;
    wire[1] = (index % 2) ? 2 : 1;
    wire[2] = 1;
    wire[3] = 0;
    if (!(index % 2)) {
        wire[4] = (uint8_t)((index / 2 + revision) % 2);
        return 5;
    }
    uint32_t value = UINT32_C(0x12345678) + index + revision;
    for (unsigned i = 0; i < 4; ++i) {
        wire[4 + i] = (uint8_t)(value >> (8 * i));
    }
    return 8;
}
