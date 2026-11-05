/*
 * The derive, random and keyed hash ports of crux_access over PSA Crypto, on the sequences that
 * tests/psa_crypto qualifies on both providers. Only this file sees the provider's header. Any
 * failure is false, which the core reports as NOT_READY with no guess evaluated.
 */
#include "access_port.h"

#include <psa/crypto.h>

#include <string.h>

static void wipe(void *buffer, size_t length)
{
    volatile uint8_t *bytes = buffer;
    while (length-- > 0u) {
        *bytes++ = 0u;
    }
}

bool crux_access_port_derive(uint32_t algorithm, uint32_t iterations,
                             const uint8_t salt[CRUX_ACCESS_SALT_SIZE], const char *password,
                             size_t length, uint8_t derived[CRUX_ACCESS_DERIVED_SIZE])
{
    if (algorithm != CRUX_ACCESS_VERIFIER_PBKDF2_HMAC_SHA256 || psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }
    psa_key_derivation_operation_t operation = PSA_KEY_DERIVATION_OPERATION_INIT;
    /* An empty password reaches this from a login with no password, so it has no bytes to point to.
     */
    const uint8_t *secret = length > 0u ? (const uint8_t *)password : (const uint8_t *)"";
    psa_status_t status =
        psa_key_derivation_setup(&operation, PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256));
    if (status == PSA_SUCCESS) {
        status =
            psa_key_derivation_input_integer(&operation, PSA_KEY_DERIVATION_INPUT_COST, iterations);
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SALT, salt,
                                                CRUX_ACCESS_SALT_SIZE);
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_PASSWORD,
                                                secret, length);
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_output_bytes(&operation, derived, CRUX_ACCESS_DERIVED_SIZE);
    }
    (void)psa_key_derivation_abort(&operation);
    if (status != PSA_SUCCESS) {
        wipe(derived, CRUX_ACCESS_DERIVED_SIZE);
        return false;
    }
    return true;
}

bool crux_access_port_random(uint8_t *output, size_t length)
{
    return psa_crypto_init() == PSA_SUCCESS && psa_generate_random(output, length) == PSA_SUCCESS;
}

bool crux_access_port_hmac(const uint8_t key[CRUX_ACCESS_HASH_KEY_SIZE], const uint8_t *data,
                           size_t length, uint8_t hash[CRUX_ACCESS_HASH_SIZE])
{
    if (psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }
    psa_key_attributes_t attributes = psa_key_attributes_init();
    psa_key_id_t imported = PSA_KEY_ID_NULL;
    size_t written = 0;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&attributes, CRUX_ACCESS_HASH_KEY_SIZE * 8u);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attributes, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    psa_status_t status = psa_import_key(&attributes, key, CRUX_ACCESS_HASH_KEY_SIZE, &imported);
    psa_reset_key_attributes(&attributes);
    if (status == PSA_SUCCESS) {
        status = psa_mac_compute(imported, PSA_ALG_HMAC(PSA_ALG_SHA_256),
                                 length > 0u ? data : (const uint8_t *)"", length, hash,
                                 CRUX_ACCESS_HASH_SIZE, &written);
    }
    /* A key table slot is scarce and shared with TLS on the Mbed profile, so none stays behind. */
    (void)psa_destroy_key(imported);
    return status == PSA_SUCCESS && written == CRUX_ACCESS_HASH_SIZE;
}
