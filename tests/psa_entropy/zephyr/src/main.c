/* SPDX-License-Identifier: Apache-2.0 */
/* What Zephyr's Mbed TLS glue does when the entropy device fails.
 *
 * Mbed TLS seeds the PSA random generator from the entropy device. Zephyr's glue, unchanged,
 * answers a failed read with sys_rand_get() and reports full entropy, and on RT1060-EVKB
 * sys_rand_get() is not cryptographically secure. Crux requires a failed read to fail PSA
 * instead (owner decision of 6 November 2026, docs/psa-crypto-qualification.md, finding 3).
 * The three Twister configurations in testcase.yaml state the three outcomes:
 *   - a healthy device, with the correction: PSA works;
 *   - a failing device, with the correction: PSA does not initialise;
 *   - a failing device, without it: PSA initialises. This control records that the upstream
 *     behaviour exists. If it fails, upstream changed and the correction can be reconsidered.
 * Every outcome also requires that the device was read, so a pass cannot come from another
 * source.
 */
#include <zephyr/ztest.h>

#include <psa/crypto.h>

#include "test_entropy.h"

ZTEST(psa_entropy, test_initialisation_follows_the_entropy_read)
{
    psa_status_t status = psa_crypto_init();

    TC_PRINT("psa_crypto_init: %d, entropy reads: %u\n", (int)status, crux_test_entropy_reads());
    zassert_true(crux_test_entropy_reads() > 0U, "PSA never read the test entropy device");

    if (IS_ENABLED(CONFIG_CRUX_TEST_ENTROPY_FAILS) && IS_ENABLED(CONFIG_CRUX_FAIL_CLOSED_ENTROPY)) {
        zassert_not_equal(status, PSA_SUCCESS,
                          "PSA initialised although every entropy read failed");
        return;
    }

    zassert_equal(status, PSA_SUCCESS, "PSA did not initialise: %d", (int)status);
    uint8_t bytes[32] = {0};
    zassert_equal(psa_generate_random(bytes, sizeof(bytes)), PSA_SUCCESS, "no random bytes");
}

ZTEST_SUITE(psa_entropy, NULL, NULL, NULL, NULL, NULL);
