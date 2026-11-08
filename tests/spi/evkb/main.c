/* Board cross-build only: this application is compiled and linked, never flashed or run. */
#include "spi_flash_id.h"

int main(void)
{
    struct spi_flash_jedec_id id;
    return spi_flash_read_jedec_id(&id) == CRUX_OK ? 0 : 1;
}
