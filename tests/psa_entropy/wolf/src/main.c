/* SPDX-License-Identifier: Apache-2.0 */
/* What the Wolf profile does when the entropy device fails, on native_sim.
 *
 * Crux requires a failed random read to produce nothing: no key, no certificate serial, no salt, no
 * session identifier, and a boot line that says so (docs/rt1060-wolf-entropy.md). wolfCrypt's
 * Hash-DRBG is seeded from the chosen zephyr,entropy device by wc_GenerateSeed(), each time a
 * generator is made, and a failed read is RNG_FAILURE_E. These cases run the real Crux PSA ports
 * over wolfPSA with a device that fails. One image per variant (CRUX_WOLF_ENTROPY_VARIANT):
 *
 *   runtime     a healthy start, a failed first read, and a read that fails after a
 *               successful start;
 *   init-fails  a device that does not start, so it is not ready;
 *   fallback    CONTROL: with no entropy driver wolfCrypt calls sys_rand_get() and succeeds,
 *               whatever the hardware would do. The boot check refuses to compile in that
 *               composition (variant guard).
 *
 * Every case requires that the device was read where it is read at all, so a pass cannot come from
 * another source.
 */
#include <zephyr/ztest.h>

#include <psa/crypto.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "access_port.h"
#include "credential_port.h"
#include "entropy_boot.h"
#include "test_entropy.h"
#include "tls_certificate_port.h"
#include "web_session_port.h"

#define EXPECTED_OK_LINE                                                                           \
    "CRUX-ENTROPY first-read=ok source=crux-test-entropy compatible=crux,test-entropy bytes=32 "   \
    "status=0 "                                                                                    \
    "device-ready=1"

static bool all_zero(const uint8_t *bytes, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        if (bytes[i] != 0u) {
            return false;
        }
    }
    return true;
}

/* Every place in the Crux code that asks for random bytes or a key, and what each does. 'works'
 * says which outcome is required. A failure must leave the caller's buffers untouched or wiped,
 * which is zero here. */
static void consumers(bool works)
{
    uint8_t bytes[32] = {0};

    zassert_equal(psa_crypto_init(), PSA_SUCCESS, "wolfPSA did not initialise");
    zassert_equal(psa_generate_random(bytes, sizeof(bytes)) == PSA_SUCCESS, works,
                  "psa_generate_random: works=%d", (int)works);
    zassert_equal(all_zero(bytes, sizeof(bytes)), !works,
                  "psa_generate_random wrote bytes on failure");

    memset(bytes, 0, sizeof(bytes));
    zassert_equal(crux_access_port_random(bytes, sizeof(bytes)), works, "access salt");
    zassert_equal(all_zero(bytes, sizeof(bytes)), !works, "access salt bytes");
    memset(bytes, 0, sizeof(bytes));
    zassert_equal(crux_tls_certificate_port_random(bytes, sizeof(bytes)), works,
                  "certificate serial");
    zassert_equal(all_zero(bytes, sizeof(bytes)), !works, "certificate serial bytes");
    memset(bytes, 0, sizeof(bytes));
    zassert_equal(crux_web_session_port_random(bytes, sizeof(bytes)), works, "session identifier");
    zassert_equal(all_zero(bytes, sizeof(bytes)), !works, "session identifier bytes");
    memset(bytes, 0, sizeof(bytes));
    zassert_equal(crux_credential_port_random(bytes, sizeof(bytes)),
                  works ? CRUX_CREDENTIAL_OK : CRUX_CREDENTIAL_ERR_ENTROPY, "nonce");
    zassert_equal(all_zero(bytes, sizeof(bytes)), !works, "nonce bytes");

    /* The TLS key (the certificate's) and the SSH host key. */
    static const crux_credential_key_type_t types[] = {CRUX_CREDENTIAL_KEY_ECDSA_P256,
                                                       CRUX_CREDENTIAL_KEY_ED25519};
    for (size_t i = 0; i < ARRAY_SIZE(types); i++) {
        uint8_t public_key[80] = {0};
        uint8_t private_key[80] = {0};
        size_t lengths[2] = {0, 0};
        crux_credential_result_t result = crux_credential_port_generate_key(
            types[i], public_key, sizeof(public_key), private_key, sizeof(private_key), lengths);
        zassert_equal(result, works ? CRUX_CREDENTIAL_OK : CRUX_CREDENTIAL_ERR_ENTROPY,
                      "key type %d", (int)types[i]);
        zassert_equal(all_zero(public_key, sizeof(public_key)), !works, "public key bytes");
        zassert_equal(all_zero(private_key, sizeof(private_key)), !works, "private key bytes");
        zassert_equal(lengths[0] != 0u, works, "key lengths");
    }
}

static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    crux_test_entropy_set_failing(false);
}

#if defined(CRUX_WOLF_ENTROPY_CONTROL)

ZTEST(wolf_entropy, test_without_an_entropy_driver_wolfcrypt_falls_back_to_sys_rand_get)
{
    unsigned int before_reads = crux_test_entropy_reads();

    crux_test_entropy_set_failing(true);
    consumers(true);
    zassert_equal(crux_test_entropy_reads(), before_reads,
                  "a device was read although the image has no entropy driver");
}

#elif defined(CONFIG_CRUX_TEST_ENTROPY_INIT_FAILS)

/* The consumers, with no boot check involved: what the Wolf profile does by itself. */
ZTEST(wolf_entropy, test_consumers_produce_nothing_when_the_device_is_not_ready)
{
    unsigned int before_reads = crux_test_entropy_reads();

    consumers(false);
    zassert_equal(crux_test_entropy_reads(), before_reads,
                  "a device that is not ready was read: wolfCrypt must check it first");
}

ZTEST(wolf_entropy, test_boot_check_refuses_when_the_device_is_not_ready)
{
    zassert_false(crux_entropy_boot_check(),
                  "the boot check passed with a device that is not ready");
    const char *line = crux_entropy_boot_last_line();
    zassert_true(strncmp(line, "CRUX-ENTROPY first-read=failed source=crux-test-entropy ", 55) == 0,
                 "%s", line);
    zassert_not_null(strstr(line, " compatible=crux,test-entropy bytes=32 status="), "%s", line);
    zassert_not_null(strstr(line, " device-ready=0"), "%s", line);
}

#else

/* The consumers, with no boot check involved: what the Wolf profile does by itself. */
ZTEST(wolf_entropy, test_consumers_work_with_a_healthy_device)
{
    unsigned int before_reads = crux_test_entropy_reads();

    consumers(true);
    zassert_true(crux_test_entropy_reads() > before_reads,
                 "the consumers never reached the device");
}

ZTEST(wolf_entropy, test_consumers_produce_nothing_when_every_read_fails)
{
    unsigned int before_reads = crux_test_entropy_reads();

    crux_test_entropy_set_failing(true);
    consumers(false);
    zassert_true(crux_test_entropy_reads() > before_reads,
                 "the failed reads never reached the device");
}

/* Each psa_generate_random seeds a new Hash-DRBG from the device, so a read that fails after a
 * successful start reaches the same function as the first one. This is not wolfCrypt's reseed
 * inside one generator, which a million requests later is not exercised here. */
ZTEST(wolf_entropy, test_consumers_fail_when_a_read_fails_after_a_successful_start)
{
    consumers(true);
    crux_test_entropy_set_failing(true);
    consumers(false);
    crux_test_entropy_set_failing(false);
    consumers(true);
}

ZTEST(wolf_entropy, test_boot_check_healthy_prints_the_ok_line)
{
    unsigned int before_reads = crux_test_entropy_reads();

    zassert_true(crux_entropy_boot_check(), "the boot check failed with a healthy device");
    zassert_str_equal(crux_entropy_boot_last_line(), EXPECTED_OK_LINE);
    zassert_true(crux_test_entropy_reads() > before_reads,
                 "the boot read never reached the device");
}

ZTEST(wolf_entropy, test_boot_check_refuses_when_the_first_read_fails)
{
    unsigned int before_reads = crux_test_entropy_reads();

    crux_test_entropy_set_failing(true);
    zassert_false(crux_entropy_boot_check(), "the boot check passed although every read fails");
    const char *line = crux_entropy_boot_last_line();
    zassert_true(strncmp(line, "CRUX-ENTROPY first-read=failed source=crux-test-entropy ", 55) == 0,
                 "%s", line);
    zassert_not_null(strstr(line, " compatible=crux,test-entropy bytes=32 status="), "%s", line);
    zassert_not_null(strstr(line, " device-ready=1"), "%s", line);
    const char *status = strstr(line, "status=");
    zassert_not_null(status);
    zassert_not_equal(atoi(status + 7), 0, "a failed read reports status 0: %s", line);
    zassert_true(crux_test_entropy_reads() > before_reads,
                 "the failed read never reached the device");
}

#endif

ZTEST_SUITE(wolf_entropy, NULL, NULL, before, NULL, NULL);
