#include "tmp117.h"

#include <crux/i2c.h>
#include <stddef.h>

#define REGISTER_TEMPERATURE UINT8_C(0x00)
#define REGISTER_DEVICE_ID UINT8_C(0x0F)
#define DEVICE_ID_MASK UINT16_C(0x0FFF)
#define DEVICE_ID_TMP117 UINT16_C(0x0117)
#define RAW_NO_CONVERSION INT16_C(-32768)

/* One LSB is 1/128 degree Celsius, which is 125/16 thousandths of a degree. */
#define MILLIDEGREES_PER_LSB_NUMERATOR 125
#define MILLIDEGREES_PER_LSB_DENOMINATOR 16

static crux_status_t read_register(uint16_t address, uint8_t reg, uint16_t *value)
{
    uint8_t raw[2];
    crux_status_t result = crux_i2c_transfer(address, &reg, sizeof(reg), raw, sizeof(raw));
    if (result == CRUX_OK) {
        *value = (uint16_t)((uint16_t)raw[0] << 8 | raw[1]);
    }
    return result;
}

crux_status_t tmp117_probe(uint16_t address)
{
    uint16_t id = 0;
    crux_status_t result = read_register(address, REGISTER_DEVICE_ID, &id);
    if (result != CRUX_OK) {
        return result;
    }
    return (id & DEVICE_ID_MASK) == DEVICE_ID_TMP117 ? CRUX_OK : CRUX_ERR_IO;
}

crux_status_t tmp117_read_millidegrees(uint16_t address, int32_t *millidegrees)
{
    if (millidegrees == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }

    uint16_t unsigned_raw = 0;
    crux_status_t result = read_register(address, REGISTER_TEMPERATURE, &unsigned_raw);
    if (result != CRUX_OK) {
        return result;
    }

    /* Reinterpret the two's-complement register without relying on implementation-defined casts. */
    int32_t raw =
        unsigned_raw >= UINT16_C(0x8000) ? (int32_t)unsigned_raw - 65536 : (int32_t)unsigned_raw;
    if (raw == RAW_NO_CONVERSION) {
        return CRUX_ERR_NO_DATA;
    }
    *millidegrees = raw * MILLIDEGREES_PER_LSB_NUMERATOR / MILLIDEGREES_PER_LSB_DENOMINATOR;
    return CRUX_OK;
}
