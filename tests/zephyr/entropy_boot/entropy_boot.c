/* SPDX-License-Identifier: Apache-2.0 */
/* The boot check of the random source: see entropy_boot.h. */
#include "entropy_boot.h"

#include <psa/crypto.h>

#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>
#include <zephyr/toolchain.h>

/*
 * wolfCrypt's Zephyr seed function, wc_GenerateSeed(), reads the chosen zephyr,entropy device only
 * when CONFIG_ENTROPY_HAS_DRIVER is set and the devicetree has that chosen node. In any other build
 * it calls sys_rand_get(), which is a general-purpose generator (and on the RT1060-EVKB is not a
 * cryptographic one), and it succeeds whatever the hardware does. A composition in that state would
 * pass the check below and make keys from the wrong source, so it does not compile. Neither does
 * one that selects Zephyr's test generator.
 */
#if !(defined(CONFIG_ENTROPY_HAS_DRIVER) && DT_HAS_CHOSEN(zephyr_entropy))
#error                                                                                             \
    "wolfCrypt would seed from sys_rand_get(): this build needs an entropy driver and a chosen zephyr,entropy node"
#endif
BUILD_ASSERT(!IS_ENABLED(CONFIG_TEST_RANDOM_GENERATOR),
             "wolfCrypt would seed from sys_rand_get(): a test random generator is selected");

#define SOURCE DT_CHOSEN(zephyr_entropy)

static char last_line[160];

bool crux_entropy_boot_check(void)
{
    const struct device *device = DEVICE_DT_GET(SOURCE);
    uint8_t bytes[CRUX_ENTROPY_BOOT_BYTES];
    psa_status_t status = psa_crypto_init();

    if (status == PSA_SUCCESS) {
        status = psa_generate_random(bytes, sizeof(bytes));
    }
    /* Used for nothing, and not left on the stack. */
    for (volatile uint8_t *wipe = bytes; wipe < bytes + sizeof(bytes); wipe++) {
        *wipe = 0u;
    }
    bool ok = status == PSA_SUCCESS;
    int length = snprintf(last_line, sizeof(last_line),
                          "CRUX-ENTROPY first-read=%s source=%s compatible=%s bytes=%u status=%d "
                          "device-ready=%d",
                          ok ? "ok" : "failed", DT_NODE_FULL_NAME(SOURCE),
                          DT_PROP_BY_IDX(SOURCE, compatible, 0), (unsigned)CRUX_ENTROPY_BOOT_BYTES,
                          (int)status, device_is_ready(device) ? 1 : 0);
    if (length < 0 || (size_t)length >= sizeof(last_line)) {
        /* Never print a cut line: the host parses it, and a line that cannot be written is a
         * failure. */
        last_line[0] = '\0';
        ok = false;
    }
    printk("%s\n",
           last_line[0] != '\0'
               ? last_line
               : "CRUX-ENTROPY first-read=failed source=unknown compatible=unknown bytes=32 "
                 "status=-1 device-ready=0");
    return ok;
}

const char *crux_entropy_boot_last_line(void)
{
    return last_line;
}
