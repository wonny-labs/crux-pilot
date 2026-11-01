#include <assert.h>
#include <crux/i2c.h>
#include <stdint.h>

int main(void)
{
    const uint8_t tx[] = {0x10, 0x20, 0x30};
    uint8_t rx[] = {0, 0, 0};
    assert(crux_i2c_transfer(0x48, tx, sizeof(tx), rx, sizeof(rx)) == CRUX_OK);
    for (size_t i = 0; i < sizeof(tx); ++i) {
        assert(rx[i] == tx[i]);
    }

    uint8_t byte = 0;
    assert(crux_i2c_transfer(0x80, &byte, 1, NULL, 0) == CRUX_ERR_OUT_OF_RANGE);
    assert(crux_i2c_transfer(0x20, NULL, 1, NULL, 0) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_i2c_transfer(0x20, NULL, 0, NULL, 1) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_i2c_transfer(0x20, NULL, 0, NULL, 0) == CRUX_ERR_INVALID_ARGUMENT);

    uint8_t long_transfer[256] = {0};
    uint8_t echoed[sizeof(long_transfer)] = {0};
    assert(crux_i2c_transfer(0x20, long_transfer, sizeof(long_transfer), echoed, sizeof(echoed)) ==
           CRUX_OK);
    for (size_t i = 0; i < sizeof(echoed); ++i) {
        assert(echoed[i] == 0);
    }

#if SIZE_MAX > UINT32_MAX
    size_t too_long = (size_t)UINT32_MAX + 1U;
    assert(crux_i2c_transfer(0x20, &byte, too_long, NULL, 0) == CRUX_ERR_OUT_OF_RANGE);
    assert(crux_i2c_transfer(0x20, NULL, 0, &byte, too_long) == CRUX_ERR_OUT_OF_RANGE);
#endif
    return 0;
}
