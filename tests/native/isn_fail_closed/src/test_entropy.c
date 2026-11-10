/* SPDX-License-Identifier: Apache-2.0 */
/* An entropy device for tests. It is not a source of entropy: the bytes repeat. While
 * crux_test_entropy_result is not zero, every read fails with that value, the way a TRNG read can
 * fail after the device has started. sys_csrand_get() is Zephyr's own
 * (CONFIG_HARDWARE_DEVICE_CS_GENERATOR), over this device.
 */
#define DT_DRV_COMPAT crux_test_entropy

#include <errno.h>
#include <stdatomic.h>

#include <zephyr/device.h>
#include <zephyr/drivers/entropy.h>

#include "test_entropy.h"

volatile int crux_test_entropy_result;
static atomic_uint reads;

unsigned int crux_test_entropy_reads(void)
{
    return atomic_load(&reads);
}

static int test_entropy_get(const struct device *dev, uint8_t *buffer, uint16_t length)
{
    ARG_UNUSED(dev);
    atomic_fetch_add(&reads, 1U);
    if (crux_test_entropy_result != 0) {
        return crux_test_entropy_result;
    }
    for (uint16_t index = 0; index < length; ++index) {
        buffer[index] = (uint8_t)(0xA5U ^ (index * 37U));
    }
    return 0;
}

static int test_entropy_get_isr(const struct device *dev, uint8_t *buffer, uint16_t length,
                                uint32_t flags)
{
    ARG_UNUSED(flags);
    return test_entropy_get(dev, buffer, length) == 0 ? (int)length : -EIO;
}

static int test_entropy_init(const struct device *dev)
{
    ARG_UNUSED(dev);
    return 0;
}

static DEVICE_API(entropy, test_entropy_api) = {
    .get_entropy = test_entropy_get,
    .get_entropy_isr = test_entropy_get_isr,
};

DEVICE_DT_INST_DEFINE(0, test_entropy_init, NULL, NULL, NULL, POST_KERNEL,
                      CONFIG_ENTROPY_INIT_PRIORITY, &test_entropy_api);
