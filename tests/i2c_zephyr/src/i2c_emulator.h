#ifndef CRUX_I2C_EMULATOR_H
#define CRUX_I2C_EMULATOR_H

#include <stdbool.h>

struct crux_i2c_emulator_test_api {
    int (*message_count)(void);
    bool (*used_repeated_start)(void);
};

#endif
