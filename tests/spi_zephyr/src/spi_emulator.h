#ifndef CRUX_SPI_EMULATOR_H
#define CRUX_SPI_EMULATOR_H

#include <stddef.h>
#include <stdint.h>

struct crux_spi_emulator_test_api {
    int (*call_count)(void);
    uint32_t (*frequency)(void);
    uint32_t (*operation)(void);
    uint16_t (*word_size)(void);
    uint16_t (*slave)(void);
};

#endif
