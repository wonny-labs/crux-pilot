/*
 * Test-only stub providers for the PSA Crypto conformance cases (Issue #142).
 *
 * Neither stub is a provider. They exist so the cases can be shown to fail
 * before a real provider is wired in:
 *   - CRUX_PSA_STUB_PERMISSIVE=0: initialisation succeeds and every other call returns
 *     PSA_ERROR_NOT_SUPPORTED, so each case reaches what it tests. Only the cases that expect that
 *     status may pass.
 *   - CRUX_PSA_STUB_PERMISSIVE=1: every call returns PSA_SUCCESS with zeroed
 *     output. Every case that expects an error status must fail.
 * Only the PSA API headers of the provider tree are used; none of its sources
 * are compiled.
 */
#include <psa/crypto.h>

#include <string.h>

#ifndef CRUX_PSA_STUB_PERMISSIVE
#error "CRUX_PSA_STUB_PERMISSIVE must be defined as 0 or 1"
#endif

#if CRUX_PSA_STUB_PERMISSIVE
#define STUB_STATUS PSA_SUCCESS
#define STUB_LENGTH(value) (value)
#else
#define STUB_STATUS PSA_ERROR_NOT_SUPPORTED
#define STUB_LENGTH(value) ((size_t)0)
#endif

psa_status_t psa_crypto_init(void)
{
    return PSA_SUCCESS;
}

psa_status_t psa_generate_random(uint8_t *output, size_t output_size)
{
    memset(output, 0, output_size);
    return STUB_STATUS;
}

psa_status_t psa_hash_compute(psa_algorithm_t alg, const uint8_t *input, size_t input_length,
                              uint8_t *hash, size_t hash_size, size_t *hash_length)
{
    (void)alg;
    (void)input;
    (void)input_length;
    memset(hash, 0, hash_size);
    *hash_length = STUB_LENGTH(hash_size);
    return STUB_STATUS;
}

psa_status_t psa_hash_setup(psa_hash_operation_t *operation, psa_algorithm_t alg)
{
    (void)operation;
    (void)alg;
    return STUB_STATUS;
}

psa_status_t psa_hash_update(psa_hash_operation_t *operation, const uint8_t *input,
                             size_t input_length)
{
    (void)operation;
    (void)input;
    (void)input_length;
    return STUB_STATUS;
}

psa_status_t psa_hash_finish(psa_hash_operation_t *operation, uint8_t *hash, size_t hash_size,
                             size_t *hash_length)
{
    (void)operation;
    memset(hash, 0, hash_size);
    *hash_length = STUB_LENGTH(hash_size);
    return STUB_STATUS;
}

psa_status_t psa_hash_verify(psa_hash_operation_t *operation, const uint8_t *hash,
                             size_t hash_length)
{
    (void)operation;
    (void)hash;
    (void)hash_length;
    return STUB_STATUS;
}

psa_status_t psa_hash_abort(psa_hash_operation_t *operation)
{
    (void)operation;
    return PSA_SUCCESS;
}

psa_status_t psa_import_key(const psa_key_attributes_t *attributes, const uint8_t *data,
                            size_t data_length, psa_key_id_t *key)
{
    (void)attributes;
    (void)data;
    (void)data_length;
    *key = STUB_LENGTH((psa_key_id_t)1);
    return STUB_STATUS;
}

psa_status_t psa_get_key_attributes(psa_key_id_t key, psa_key_attributes_t *attributes)
{
    (void)key;
    *attributes = psa_key_attributes_init();
    return STUB_STATUS;
}

psa_status_t psa_destroy_key(psa_key_id_t key)
{
    (void)key;
    return STUB_STATUS;
}

psa_status_t psa_verify_hash(psa_key_id_t key, psa_algorithm_t alg, const uint8_t *hash,
                             size_t hash_length, const uint8_t *signature, size_t signature_length)
{
    (void)key;
    (void)alg;
    (void)hash;
    (void)hash_length;
    (void)signature;
    (void)signature_length;
    return STUB_STATUS;
}

psa_status_t psa_key_derivation_setup(psa_key_derivation_operation_t *operation,
                                      psa_algorithm_t alg)
{
    (void)operation;
    (void)alg;
    return STUB_STATUS;
}

psa_status_t psa_key_derivation_input_integer(psa_key_derivation_operation_t *operation,
                                              psa_key_derivation_step_t step, uint64_t value)
{
    (void)operation;
    (void)step;
    (void)value;
    return STUB_STATUS;
}

psa_status_t psa_key_derivation_input_bytes(psa_key_derivation_operation_t *operation,
                                            psa_key_derivation_step_t step, const uint8_t *data,
                                            size_t data_length)
{
    (void)operation;
    (void)step;
    (void)data;
    (void)data_length;
    return STUB_STATUS;
}

psa_status_t psa_key_derivation_output_bytes(psa_key_derivation_operation_t *operation,
                                             uint8_t *output, size_t output_length)
{
    (void)operation;
    memset(output, 0, output_length);
    return STUB_STATUS;
}

psa_status_t psa_key_derivation_verify_bytes(psa_key_derivation_operation_t *operation,
                                             const uint8_t *expected_output, size_t output_length)
{
    (void)operation;
    (void)expected_output;
    (void)output_length;
    return STUB_STATUS;
}

psa_status_t psa_key_derivation_abort(psa_key_derivation_operation_t *operation)
{
    (void)operation;
    return PSA_SUCCESS;
}

void psa_reset_key_attributes(psa_key_attributes_t *attributes)
{
    *attributes = psa_key_attributes_init();
}

psa_status_t psa_generate_key(const psa_key_attributes_t *attributes, psa_key_id_t *key)
{
    (void)attributes;
    *key = STUB_LENGTH((psa_key_id_t)1);
    return STUB_STATUS;
}

psa_status_t psa_export_key(psa_key_id_t key, uint8_t *data, size_t data_size, size_t *data_length)
{
    (void)key;
    memset(data, 0, data_size);
    *data_length = STUB_LENGTH(data_size);
    return STUB_STATUS;
}

psa_status_t psa_export_public_key(psa_key_id_t key, uint8_t *data, size_t data_size,
                                   size_t *data_length)
{
    (void)key;
    memset(data, 0, data_size);
    *data_length = STUB_LENGTH(data_size);
    return STUB_STATUS;
}

psa_status_t psa_sign_hash(psa_key_id_t key, psa_algorithm_t alg, const uint8_t *hash,
                           size_t hash_length, uint8_t *signature, size_t signature_size,
                           size_t *signature_length)
{
    (void)key;
    (void)alg;
    (void)hash;
    (void)hash_length;
    memset(signature, 0, signature_size);
    *signature_length = STUB_LENGTH(signature_size);
    return STUB_STATUS;
}

psa_status_t psa_sign_message(psa_key_id_t key, psa_algorithm_t alg, const uint8_t *input,
                              size_t input_length, uint8_t *signature, size_t signature_size,
                              size_t *signature_length)
{
    (void)key;
    (void)alg;
    (void)input;
    (void)input_length;
    memset(signature, 0, signature_size);
    *signature_length = STUB_LENGTH(signature_size);
    return STUB_STATUS;
}

psa_status_t psa_verify_message(psa_key_id_t key, psa_algorithm_t alg, const uint8_t *input,
                                size_t input_length, const uint8_t *signature,
                                size_t signature_length)
{
    (void)key;
    (void)alg;
    (void)input;
    (void)input_length;
    (void)signature;
    (void)signature_length;
    return STUB_STATUS;
}

psa_status_t psa_mac_compute(psa_key_id_t key, psa_algorithm_t alg, const uint8_t *input,
                             size_t input_length, uint8_t *mac, size_t mac_size, size_t *mac_length)
{
    (void)key;
    (void)alg;
    (void)input;
    (void)input_length;
    memset(mac, 0, mac_size);
    *mac_length = STUB_LENGTH(mac_size);
    return STUB_STATUS;
}

psa_status_t psa_aead_encrypt(psa_key_id_t key, psa_algorithm_t alg, const uint8_t *nonce,
                              size_t nonce_length, const uint8_t *additional_data,
                              size_t additional_data_length, const uint8_t *plaintext,
                              size_t plaintext_length, uint8_t *ciphertext, size_t ciphertext_size,
                              size_t *ciphertext_length)
{
    (void)key;
    (void)alg;
    (void)nonce;
    (void)nonce_length;
    (void)additional_data;
    (void)additional_data_length;
    (void)plaintext;
    (void)plaintext_length;
    memset(ciphertext, 0, ciphertext_size);
    *ciphertext_length = STUB_LENGTH(ciphertext_size);
    return STUB_STATUS;
}

psa_status_t psa_aead_decrypt(psa_key_id_t key, psa_algorithm_t alg, const uint8_t *nonce,
                              size_t nonce_length, const uint8_t *additional_data,
                              size_t additional_data_length, const uint8_t *ciphertext,
                              size_t ciphertext_length, uint8_t *plaintext, size_t plaintext_size,
                              size_t *plaintext_length)
{
    (void)key;
    (void)alg;
    (void)nonce;
    (void)nonce_length;
    (void)additional_data;
    (void)additional_data_length;
    (void)ciphertext;
    (void)ciphertext_length;
    memset(plaintext, 0, plaintext_size);
    *plaintext_length = STUB_LENGTH(plaintext_size);
    return STUB_STATUS;
}

psa_status_t psa_aead_encrypt_setup(psa_aead_operation_t *operation, psa_key_id_t key,
                                    psa_algorithm_t alg)
{
    (void)operation;
    (void)key;
    (void)alg;
    return STUB_STATUS;
}

psa_status_t psa_aead_decrypt_setup(psa_aead_operation_t *operation, psa_key_id_t key,
                                    psa_algorithm_t alg)
{
    (void)operation;
    (void)key;
    (void)alg;
    return STUB_STATUS;
}

psa_status_t psa_aead_set_lengths(psa_aead_operation_t *operation, size_t ad_length,
                                  size_t plaintext_length)
{
    (void)operation;
    (void)ad_length;
    (void)plaintext_length;
    return STUB_STATUS;
}

psa_status_t psa_aead_set_nonce(psa_aead_operation_t *operation, const uint8_t *nonce,
                                size_t nonce_length)
{
    (void)operation;
    (void)nonce;
    (void)nonce_length;
    return STUB_STATUS;
}

psa_status_t psa_aead_update_ad(psa_aead_operation_t *operation, const uint8_t *input,
                                size_t input_length)
{
    (void)operation;
    (void)input;
    (void)input_length;
    return STUB_STATUS;
}

psa_status_t psa_aead_update(psa_aead_operation_t *operation, const uint8_t *input,
                             size_t input_length, uint8_t *output, size_t output_size,
                             size_t *output_length)
{
    (void)operation;
    (void)input;
    (void)input_length;
    memset(output, 0, output_size);
    *output_length = STUB_LENGTH(input_length);
    return STUB_STATUS;
}

psa_status_t psa_aead_finish(psa_aead_operation_t *operation, uint8_t *ciphertext,
                             size_t ciphertext_size, size_t *ciphertext_length, uint8_t *tag,
                             size_t tag_size, size_t *tag_length)
{
    (void)operation;
    memset(ciphertext, 0, ciphertext_size);
    memset(tag, 0, tag_size);
    *ciphertext_length = 0;
    *tag_length = STUB_LENGTH(tag_size);
    return STUB_STATUS;
}

psa_status_t psa_aead_verify(psa_aead_operation_t *operation, uint8_t *plaintext,
                             size_t plaintext_size, size_t *plaintext_length, const uint8_t *tag,
                             size_t tag_length)
{
    (void)operation;
    (void)tag;
    (void)tag_length;
    memset(plaintext, 0, plaintext_size);
    *plaintext_length = 0;
    return STUB_STATUS;
}

psa_status_t psa_aead_abort(psa_aead_operation_t *operation)
{
    (void)operation;
    return PSA_SUCCESS;
}
