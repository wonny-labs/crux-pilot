#ifndef CRUX_TMP117_EMULATOR_H
#define CRUX_TMP117_EMULATOR_H

#include <stdint.h>

struct crux_tmp117_emulator_test_api {
    void (*set_register)(uint8_t reg, uint16_t value);
    int (*transfer_count)(void);
    /* Number of transfers that were not a one-byte write followed by a restarted two-byte read. */
    int (*malformed_count)(void);
};

#endif
