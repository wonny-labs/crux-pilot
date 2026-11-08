#ifndef CRUX_SPI_FLASH_EMULATOR_H
#define CRUX_SPI_FLASH_EMULATOR_H

#include <stdint.h>

struct crux_spi_flash_emulator_test_api {
    void (*set_id)(uint8_t manufacturer, uint8_t memory_type, uint8_t capacity_code);
    /* A non-zero value makes every exchange fail with that native result. */
    void (*set_native_result)(int result);
    int (*exchange_count)(void);
    /* Exchanges that were not a four-byte 0x9F command. */
    int (*unrecognised_count)(void);
    uint32_t (*operation)(void);
};

#endif
