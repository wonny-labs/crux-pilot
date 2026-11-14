/* SPDX-License-Identifier: Apache-2.0 */
/* An entropy device for tests. It is not a source of entropy: the bytes are a fixed function of
 * their position. Reads can be made to fail the way a TRNG read can (-EIO), at any moment, and with
 * CONFIG_CRUX_TEST_ENTROPY_INIT_FAILS the device does not initialise, so it is not ready. It counts
 * the reads, so a test can show that the code under test went through it.
 */
#define DT_DRV_COMPAT crux_test_entropy

#include <errno.h>
#include <stdatomic.h>

#include <zephyr/device.h>
#include <zephyr/drivers/entropy.h>

#include "test_entropy.h"

static atomic_uint reads;
static atomic_bool failing;

unsigned int crux_test_entropy_reads(void)
{
    return atomic_load(&reads);
}

void crux_test_entropy_set_failing(bool value)
{
    atomic_store(&failing, value);
}

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int test_entropy_get(const struct device *dev, uint8_t *buffer, uint16_t length)
{
    ARG_UNUSED(dev);
    atomic_fetch_add(&reads, 1U);
    if (atomic_load(&failing)) {
        return -EIO;
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
    return IS_ENABLED(CONFIG_CRUX_TEST_ENTROPY_INIT_FAILS) ? -EIO : 0;
}

static DEVICE_API(entropy, test_entropy_api) = {
    .get_entropy = test_entropy_get,
    .get_entropy_isr = test_entropy_get_isr,
};

DEVICE_DT_INST_DEFINE(0, test_entropy_init, NULL, NULL, NULL, PRE_KERNEL_1,
                      CONFIG_ENTROPY_INIT_PRIORITY, &test_entropy_api);

#endif
