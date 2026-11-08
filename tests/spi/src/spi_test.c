#include <assert.h>
#include <crux/spi.h>
#include <stdint.h>

int main(void)
{
    const uint8_t tx[] = {0x10, 0x20, 0x30};
    uint8_t rx[] = {0, 0, 0};
    assert(crux_spi_transceive(tx, rx, sizeof(tx)) == CRUX_OK);
    for (size_t i = 0; i < sizeof(tx); ++i) {
        assert(rx[i] == tx[i]);
    }
    assert(crux_spi_transceive(NULL, rx, sizeof(rx)) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_spi_transceive(tx, NULL, sizeof(tx)) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_spi_transceive(tx, rx, 0) == CRUX_ERR_INVALID_ARGUMENT);
    return 0;
}
