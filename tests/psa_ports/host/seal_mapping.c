/* What the seal port reports for a provider failure, over a stub provider that accepts everything,
 * with the one call that the port depends on made to fail. A real provider cannot be made to run
 * out of entropy on demand. */
#include "credential_port.h"
#include "credential_psa.h"
#include "psa_port_cases.h"
#include "vectors.h"

#include <psa/crypto.h>

#include <stdio.h>

static psa_status_t setup_answer;

psa_status_t __real_psa_aead_encrypt_setup(psa_aead_operation_t *operation, psa_key_id_t key,
                                           psa_algorithm_t algorithm);
psa_status_t __wrap_psa_aead_encrypt_setup(psa_aead_operation_t *operation, psa_key_id_t key,
                                           psa_algorithm_t algorithm);
psa_status_t __wrap_psa_aead_encrypt_setup(psa_aead_operation_t *operation, psa_key_id_t key,
                                           psa_algorithm_t algorithm)
{
    (void)operation;
    (void)key;
    (void)algorithm;
    return setup_answer;
}

static crux_credential_result_t seal_with(psa_status_t status)
{
    uint8_t sealed[sizeof(vec_seal_plain)];
    uint8_t tag[CRUX_CREDENTIAL_TAG_BYTES];
    setup_answer = status;
    return crux_credential_port_seal(vec_seal_aad, sizeof(vec_seal_aad), vec_seal_plain,
                                     sizeof(vec_seal_plain), sealed, vec_seal_nonce, tag);
}

int main(void)
{
    psa_port_test_device_id(vec_seal_device_id, sizeof(vec_seal_device_id));
    int failures = 0;
    failures += seal_with(PSA_ERROR_INSUFFICIENT_ENTROPY) != CRUX_CREDENTIAL_ERR_ENTROPY;
    failures += seal_with(PSA_ERROR_GENERIC_ERROR) != CRUX_CREDENTIAL_ERR_STORE;
    failures += seal_with(PSA_ERROR_CORRUPTION_DETECTED) != CRUX_CREDENTIAL_ERR_STORE;
    printf("seal mapping: %d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
