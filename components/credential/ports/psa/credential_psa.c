/*
 * The protection ports of the credential store over PSA Crypto, on the sequences that
 * tests/psa_crypto qualifies on both providers. Only this file sees the provider's header.
 *
 * The device key is SHA-256 of a fixed label and the device ID. It is derived for each call and its
 * key slot goes at once, because a slot is scarce and shared with TLS on the Mbed profile. Someone
 * who can read the medium and the chip ID can read every secret, as the declaration states.
 */
#include "credential_psa.h"
#include "credential_port.h"

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

#define DEVICE_KEY_LABEL "crux.credential.device-key.v1"
#define DEVICE_KEY_BYTES 32u
#define P256_PUBLIC_BYTES 65u
#define P256_PRIVATE_BYTES 32u
#define ED25519_PUBLIC_BYTES 32u
#define ED25519_PRIVATE_BYTES 32u

static void wipe(void *buffer, size_t length)
{
    volatile uint8_t *bytes = buffer;
    while (length-- > 0u) {
        *bytes++ = 0u;
    }
}

static psa_status_t device_key_import(psa_key_id_t *key)
{
    uint8_t material[sizeof(DEVICE_KEY_LABEL) - 1u + CRUX_CREDENTIAL_PSA_DEVICE_ID_MAX];
    uint8_t digest[DEVICE_KEY_BYTES];
    size_t digest_length = 0;
    memcpy(material, DEVICE_KEY_LABEL, sizeof(DEVICE_KEY_LABEL) - 1u);
    size_t id_length = crux_credential_psa_device_id(&material[sizeof(DEVICE_KEY_LABEL) - 1u]);
    if (id_length == 0u || id_length > CRUX_CREDENTIAL_PSA_DEVICE_ID_MAX) {
        return PSA_ERROR_BAD_STATE;
    }
    psa_status_t status = psa_crypto_init();
    if (status == PSA_SUCCESS) {
        status =
            psa_hash_compute(PSA_ALG_SHA_256, material, sizeof(DEVICE_KEY_LABEL) - 1u + id_length,
                             digest, sizeof(digest), &digest_length);
    }
    if (status == PSA_SUCCESS) {
        psa_key_attributes_t attributes = psa_key_attributes_init();
        psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
        psa_set_key_bits(&attributes, DEVICE_KEY_BYTES * 8u);
        psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
        psa_set_key_algorithm(&attributes, PSA_ALG_GCM);
        status = psa_import_key(&attributes, digest, sizeof(digest), key);
        psa_reset_key_attributes(&attributes);
    }
    wipe(digest, sizeof(digest));
    wipe(material, sizeof(material));
    return status;
}

const crux_credential_store_info_t *crux_credential_port_declaration(void)
{
    static const crux_credential_store_info_t declaration = {
        .basis = CRUX_CREDENTIAL_BASIS_DEVICE_ID_KEY,
        .evidence = CRUX_CREDENTIAL_EVIDENCE_SOFTWARE,
        .confidential_against_medium_reader = false,
        .forgery_detected_without_device_id = true,
        .rollback_detected = false,
    };
    psa_key_id_t key = PSA_KEY_ID_NULL;
    psa_status_t status = device_key_import(&key);
    (void)psa_destroy_key(key);
    return status == PSA_SUCCESS ? &declaration : NULL;
}

/* The multipart calls, because the port hands over the tag apart from the payload. An update may
 * hold back part of the payload, and the last call writes the rest. */
static psa_status_t seal_steps(psa_key_id_t key, const uint8_t *aad, size_t aad_length,
                               const uint8_t *plain, size_t length, uint8_t *sealed,
                               const uint8_t nonce[CRUX_CREDENTIAL_NONCE_BYTES],
                               uint8_t tag[CRUX_CREDENTIAL_TAG_BYTES])
{
    psa_aead_operation_t operation = PSA_AEAD_OPERATION_INIT;
    size_t written = 0;
    size_t last = 0;
    size_t tag_length = 0;
    psa_status_t status = psa_aead_encrypt_setup(&operation, key, PSA_ALG_GCM);
    if (status == PSA_SUCCESS) {
        status = psa_aead_set_lengths(&operation, aad_length, length);
    }
    if (status == PSA_SUCCESS) {
        status = psa_aead_set_nonce(&operation, nonce, CRUX_CREDENTIAL_NONCE_BYTES);
    }
    if (status == PSA_SUCCESS) {
        status = psa_aead_update_ad(&operation, aad, aad_length);
    }
    if (status == PSA_SUCCESS) {
        status = psa_aead_update(&operation, plain, length, sealed, length, &written);
    }
    if (status == PSA_SUCCESS) {
        status = psa_aead_finish(&operation, sealed + written, length - written, &last, tag,
                                 CRUX_CREDENTIAL_TAG_BYTES, &tag_length);
    }
    if (status == PSA_SUCCESS &&
        (written + last != length || tag_length != CRUX_CREDENTIAL_TAG_BYTES)) {
        status = PSA_ERROR_CORRUPTION_DETECTED;
    }
    (void)psa_aead_abort(&operation);
    return status;
}

crux_credential_result_t crux_credential_port_seal(const uint8_t *aad, size_t aad_length,
                                                   const uint8_t *plain, size_t length,
                                                   uint8_t *sealed,
                                                   const uint8_t nonce[CRUX_CREDENTIAL_NONCE_BYTES],
                                                   uint8_t tag[CRUX_CREDENTIAL_TAG_BYTES])
{
    psa_key_id_t key = PSA_KEY_ID_NULL;
    psa_status_t status = device_key_import(&key);
    if (status == PSA_SUCCESS) {
        status = seal_steps(key, aad, aad_length, plain, length, sealed, nonce, tag);
    }
    (void)psa_destroy_key(key);
    if (status == PSA_SUCCESS) {
        return CRUX_CREDENTIAL_OK;
    }
    return status == PSA_ERROR_INSUFFICIENT_ENTROPY ? CRUX_CREDENTIAL_ERR_ENTROPY
                                                    : CRUX_CREDENTIAL_ERR_STORE;
}

static psa_status_t open_steps(psa_key_id_t key, const uint8_t *aad, size_t aad_length,
                               const uint8_t *sealed, size_t length,
                               const uint8_t nonce[CRUX_CREDENTIAL_NONCE_BYTES],
                               const uint8_t tag[CRUX_CREDENTIAL_TAG_BYTES], uint8_t *plain)
{
    psa_aead_operation_t operation = PSA_AEAD_OPERATION_INIT;
    size_t written = 0;
    size_t last = 0;
    psa_status_t status = psa_aead_decrypt_setup(&operation, key, PSA_ALG_GCM);
    if (status == PSA_SUCCESS) {
        status = psa_aead_set_lengths(&operation, aad_length, length);
    }
    if (status == PSA_SUCCESS) {
        status = psa_aead_set_nonce(&operation, nonce, CRUX_CREDENTIAL_NONCE_BYTES);
    }
    if (status == PSA_SUCCESS) {
        status = psa_aead_update_ad(&operation, aad, aad_length);
    }
    if (status == PSA_SUCCESS) {
        status = psa_aead_update(&operation, sealed, length, plain, length, &written);
    }
    if (status == PSA_SUCCESS) {
        status = psa_aead_verify(&operation, plain + written, length - written, &last, tag,
                                 CRUX_CREDENTIAL_TAG_BYTES);
    }
    if (status == PSA_SUCCESS && written + last != length) {
        status = PSA_ERROR_CORRUPTION_DETECTED;
    }
    (void)psa_aead_abort(&operation);
    return status;
}

// The port's shape, which a provider implements, puts the nonce next to the tag.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
crux_credential_result_t crux_credential_port_open(const uint8_t *aad, size_t aad_length,
                                                   const uint8_t *sealed, size_t length,
                                                   const uint8_t nonce[CRUX_CREDENTIAL_NONCE_BYTES],
                                                   const uint8_t tag[CRUX_CREDENTIAL_TAG_BYTES],
                                                   uint8_t *plain)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    psa_key_id_t key = PSA_KEY_ID_NULL;
    psa_status_t status = device_key_import(&key);
    if (status == PSA_SUCCESS) {
        status = open_steps(key, aad, aad_length, sealed, length, nonce, tag, plain);
    }
    (void)psa_destroy_key(key);
    if (status == PSA_SUCCESS) {
        return CRUX_CREDENTIAL_OK;
    }
    /* The multipart path has already written unauthenticated bytes by the time the tag fails. */
    wipe(plain, length);
    /* Only a tag that does not verify marks the slot corrupt. A passing failure must not fall back
     * to an older generation: decision 11 of the store. */
    return status == PSA_ERROR_INVALID_SIGNATURE ? CRUX_CREDENTIAL_ERR_UNUSABLE
                                                 : CRUX_CREDENTIAL_ERR_READ;
}

crux_credential_result_t
crux_credential_port_generate_key(crux_credential_key_type_t type, uint8_t *public_key,
                                  size_t public_capacity, uint8_t *private_key,
                                  size_t private_capacity, size_t lengths[2])
{
    psa_key_attributes_t attributes = psa_key_attributes_init();
    psa_key_id_t key = PSA_KEY_ID_NULL;
    size_t public_length = 0;
    size_t private_length = 0;
    size_t public_expected = 0;
    size_t private_expected = 0;
    switch (type) {
    case CRUX_CREDENTIAL_KEY_ECDSA_P256:
        public_expected = P256_PUBLIC_BYTES;
        private_expected = P256_PRIVATE_BYTES;
        psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
        psa_set_key_bits(&attributes, 256u);
        psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_VERIFY_HASH |
                                                 PSA_KEY_USAGE_EXPORT);
        psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
        break;
    case CRUX_CREDENTIAL_KEY_ED25519:
        public_expected = ED25519_PUBLIC_BYTES;
        private_expected = ED25519_PRIVATE_BYTES;
        psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_TWISTED_EDWARDS));
        psa_set_key_bits(&attributes, 255u);
        psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE |
                                                 PSA_KEY_USAGE_VERIFY_MESSAGE |
                                                 PSA_KEY_USAGE_EXPORT);
        psa_set_key_algorithm(&attributes, PSA_ALG_PURE_EDDSA);
        break;
    default:
        return CRUX_CREDENTIAL_ERR_ENTROPY;
    }
    psa_status_t status = public_capacity >= public_expected && private_capacity >= private_expected
                              ? psa_crypto_init()
                              : PSA_ERROR_BUFFER_TOO_SMALL;
    if (status == PSA_SUCCESS) {
        status = psa_generate_key(&attributes, &key);
    }
    psa_reset_key_attributes(&attributes);
    if (status == PSA_SUCCESS) {
        status = psa_export_key(key, private_key, private_capacity, &private_length);
    }
    if (status == PSA_SUCCESS) {
        status = psa_export_public_key(key, public_key, public_capacity, &public_length);
    }
    (void)psa_destroy_key(key);
    if (status != PSA_SUCCESS || public_length != public_expected ||
        private_length != private_expected) {
        wipe(private_key, private_capacity);
        wipe(public_key, public_capacity);
        return CRUX_CREDENTIAL_ERR_ENTROPY;
    }
    lengths[0] = public_length;
    lengths[1] = private_length;
    return CRUX_CREDENTIAL_OK;
}

crux_credential_result_t crux_credential_port_random(uint8_t *buffer, size_t length)
{
    return psa_crypto_init() == PSA_SUCCESS && psa_generate_random(buffer, length) == PSA_SUCCESS
               ? CRUX_CREDENTIAL_OK
               : CRUX_CREDENTIAL_ERR_ENTROPY;
}
