/* SPDX-License-Identifier: Apache-2.0 */
#include "records.h"
#include <assert.h>
#include <string.h>
int main(void)
{
    const uint8_t bool_zero[] = {1, 1, 1, 0, 0};
    const uint8_t bool_one[] = {1, 1, 1, 0, 1};
    const uint8_t u32_first[] = {1, 2, 1, 0, 0x79, 0x56, 0x34, 0x12};
    const uint8_t u32_update[] = {1, 2, 1, 0, 0x7a, 0x56, 0x34, 0x12};
    uint8_t wire[8];
    assert(probe_record(0, 0, wire) == sizeof(bool_zero));
    assert(memcmp(wire, bool_zero, sizeof(bool_zero)) == 0);
    assert(probe_record(0, 1, wire) == sizeof(bool_one));
    assert(memcmp(wire, bool_one, sizeof(bool_one)) == 0);
    assert(probe_record(1, 0, wire) == sizeof(u32_first));
    assert(memcmp(wire, u32_first, sizeof(u32_first)) == 0);
    assert(probe_record(1, 1, wire) == sizeof(u32_update));
    assert(memcmp(wire, u32_update, sizeof(u32_update)) == 0);
    return 0;
}
