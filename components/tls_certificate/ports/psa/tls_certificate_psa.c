/*
 * The hash, signature and random ports of the TLS certificate module over PSA Crypto. Only this
 * file sees the provider's header. A composition compiles it with the module's src directory.
 *
 * psa_sign_hash is outside the subset that Issue #142 qualified for a consumer: there it ran only
 * in tests, on both providers. See docs/tls-certificate.md. The key slot goes at once after each
 * signature, because a slot is scarce and on the Mbed profile shared with TLS.
 */
#include "tls_certificate_port.h"

#include <psa/crypto.h>

#include <string.h>

/*
 * Keys from a test generator repeat, so a board must never build with one. native_sim has no other,
 * and is declared test-only: see docs/psa-crypto-design.md. A Host build has no such setting.
 */
#if !defined(CONFIG_BOARD_NATIVE_SIM) &&                                                           \
    (defined(CONFIG_FAKE_ENTROPY_NATIVE_SIM) || defined(CONFIG_TEST_RANDOM_GENERATOR))
#error "A test random generator must not reach a build of these ports for a board"
#endif

bool crux_tls_certificate_port_sha256(const uint8_t *data, size_t length, uint8_t digest[32])
{
    size_t written = 0;
    return psa_crypto_init() == PSA_SUCCESS &&
           psa_hash_compute(PSA_ALG_SHA_256, data, length, digest, 32, &written) == PSA_SUCCESS &&
           written == 32u;
}

bool crux_tls_certificate_port_sign(const uint8_t digest[32], uint8_t signature[64],
                                    const uint8_t private_key[32])
{
    psa_key_attributes_t attributes = psa_key_attributes_init();
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256u);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_HASH);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_key_id_t key = PSA_KEY_ID_NULL;
    size_t written = 0;
    psa_status_t status = psa_crypto_init();
    if (status == PSA_SUCCESS) {
        status = psa_import_key(&attributes, private_key, 32, &key);
    }
    psa_reset_key_attributes(&attributes);
    if (status == PSA_SUCCESS) {
        status =
            psa_sign_hash(key, PSA_ALG_ECDSA(PSA_ALG_SHA_256), digest, 32, signature, 64, &written);
    }
    (void)psa_destroy_key(key);
    if (status != PSA_SUCCESS || written != 64u) {
        memset(signature, 0, 64);
        return false;
    }
    return true;
}

bool crux_tls_certificate_port_random(uint8_t *buffer, size_t length)
{
    return psa_crypto_init() == PSA_SUCCESS && psa_generate_random(buffer, length) == PSA_SUCCESS;
}
