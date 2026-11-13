/* SPDX-License-Identifier: Apache-2.0 */
/*
 * The ports of the domain: everything it needs from the outside, declared by the product.
 *
 * The shell (app/src/main.c) implements these with Zephyr drivers and facilities. A Host test
 * implements them with fakes. Add a port here when the domain needs a new outside fact, and keep
 * each one narrow and in the product's own words, not the platform's.
 */
#ifndef PRODUCT_PORT_H
#define PRODUCT_PORT_H

#include <stdint.h>

/** Read the temperature in millikelvin. Return 0 on success and a negative value on failure. */
int product_port_read_temperature(int32_t *millikelvin);

#endif
