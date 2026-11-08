#ifndef CRUX_TEST_TMP117_H
#define CRUX_TEST_TMP117_H

#include <crux/status.h>
#include <stdint.h>

/*
 * A portable consumer of crux::i2c for a TI TMP117 temperature sensor. It uses only the public
 * I2C header, so it builds unchanged for the Host loopback provider and for any Zephyr
 * composition that binds crux-i2c0. It exists to exercise the I2C contract with a realistic
 * register device and is not a general sensor framework.
 */

/* Address with the ADD0 pin tied to GND; the part also answers at 0x49, 0x4A and 0x4B. */
#define TMP117_ADDRESS_GND UINT16_C(0x48)

/*
 * Confirm that the device at address identifies as a TMP117 (device ID 0x117, revision ignored).
 * One combined write-then-read transfer. Returns CRUX_ERR_IO if the device answers with another
 * identity; transfer statuses are returned unchanged.
 */
crux_status_t tmp117_probe(uint16_t address);

/*
 * Read the temperature in thousandths of a degree Celsius, truncated toward zero. One combined
 * write-then-read transfer. Returns CRUX_ERR_NO_DATA while the sensor reports that no conversion
 * has completed (register value 0x8000). *millidegrees is written only on CRUX_OK.
 */
crux_status_t tmp117_read_millidegrees(uint16_t address, int32_t *millidegrees);

#endif
