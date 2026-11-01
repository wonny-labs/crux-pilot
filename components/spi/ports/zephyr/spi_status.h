#ifndef CRUX_SPI_STATUS_H
#define CRUX_SPI_STATUS_H

#include <crux/status.h>

/* Map a native SPI driver result (0 or a negative errno) to a Crux status. */
crux_status_t crux_spi_zephyr_status(int native_result);

#endif
