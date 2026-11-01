#include "spi_flash_id.h"

#include <crux/spi.h>
#include <stdbool.h>
#include <stddef.h>

#define COMMAND_READ_JEDEC_ID UINT8_C(0x9F)
#define EXCHANGE_LENGTH 4U
#define MAX_CAPACITY_CODE 31U

crux_status_t spi_flash_read_jedec_id(struct spi_flash_jedec_id *id)
{
    if (id == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }

    const uint8_t tx[EXCHANGE_LENGTH] = {COMMAND_READ_JEDEC_ID, 0x00, 0x00, 0x00};
    uint8_t rx[EXCHANGE_LENGTH] = {0};
    crux_status_t result = crux_spi_transceive(tx, rx, sizeof(tx));
    if (result != CRUX_OK) {
        return result;
    }

    const bool all_zero = rx[1] == 0x00 && rx[2] == 0x00 && rx[3] == 0x00;
    const bool all_ones = rx[1] == 0xFF && rx[2] == 0xFF && rx[3] == 0xFF;
    if (all_zero || all_ones) {
        return CRUX_ERR_IO;
    }

    id->manufacturer = rx[1];
    id->memory_type = rx[2];
    id->capacity_code = rx[3];
    return CRUX_OK;
}

crux_status_t spi_flash_capacity_bytes(const struct spi_flash_jedec_id *id, uint32_t *bytes)
{
    if (id == NULL || bytes == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (id->capacity_code > MAX_CAPACITY_CODE) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    *bytes = UINT32_C(1) << id->capacity_code;
    return CRUX_OK;
}
