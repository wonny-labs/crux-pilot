#ifndef CRUX_TEST_SPI_FLASH_ID_H
#define CRUX_TEST_SPI_FLASH_ID_H

#include <crux/status.h>
#include <stdint.h>

/*
 * A portable consumer of crux::spi that reads the JEDEC identification of a serial NOR flash
 * (command 0x9F, as implemented by most Winbond, Macronix, GigaDevice and similar parts). It uses
 * only the public SPI header, so it builds unchanged for the Host loopback provider and for any
 * Zephyr composition that binds crux-spi0. It exercises the SPI contract with a realistic
 * command-then-response device and is not a flash driver.
 */

struct spi_flash_jedec_id {
    uint8_t manufacturer;
    uint8_t memory_type;
    uint8_t capacity_code;
};

/*
 * Read the identification with one full-duplex exchange of four bytes, so chip select stays
 * asserted for the command and the response. The first received byte is clocked while the command
 * is sent and carries no information. Returns CRUX_ERR_IO when the three identification bytes are
 * all 0x00 or all 0xFF, which is what a missing device, a floating or shorted data line or a part
 * in reset reads as. *id is written only on CRUX_OK. Provider statuses are returned unchanged.
 */
crux_status_t spi_flash_read_jedec_id(struct spi_flash_jedec_id *id);

/*
 * Memory size in bytes from the capacity code, which most parts define as the base-2 logarithm of
 * the size (0x17 is 8 MiB). Returns CRUX_ERR_OUT_OF_RANGE for a code of 32 or more, which does not
 * fit in 32 bits, and CRUX_ERR_INVALID_ARGUMENT for a null output.
 */
crux_status_t spi_flash_capacity_bytes(const struct spi_flash_jedec_id *id, uint32_t *bytes);

#endif
