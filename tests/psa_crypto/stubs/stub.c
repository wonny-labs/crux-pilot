/*
 * Test-only stub providers for the PSA Crypto conformance cases (Issue #142).
 *
 * Neither stub is a provider. They exist so the cases can be shown to fail
 * before a real provider is wired in:
 *   - CRUX_PSA_STUB_PERMISSIVE=0: every call returns PSA_ERROR_NOT_SUPPORTED.
 *     Every case must fail.
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
    return STUB_STATUS;
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

psa_status_t psa_hash_compare(psa_algorithm_t alg, const uint8_t *input, size_t input_length,
                              const uint8_t *hash, size_t hash_length)
{
    (void)alg;
    (void)input;
    (void)input_length;
    (void)hash;
    (void)hash_length;
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

psa_status_t psa_key_derivation_set_capacity(psa_key_derivation_operation_t *operation,
                                             size_t capacity)
{
    (void)operation;
    (void)capacity;
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
                                             const uint8_t *expected_output,
                                             size_t output_length)
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
