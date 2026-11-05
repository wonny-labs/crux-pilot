/* Each store port runs the fake first, for its counters and faults, and then the real provider,
 * except open, whose fake tag cannot verify a real seal, so this holds its fault. The declaration
 * stays the fake's, because these suites assert it. TF-PSA-Crypto has no Ed25519, so that key stays
 * the fake's. */
#include "credential_psa.h"
#include "fake_ports.h"

#include <string.h>

crux_credential_result_t psa_credential_port_seal(const uint8_t *aad, size_t aad_length,
                                                  const uint8_t *plain, size_t length,
                                                  uint8_t *sealed, const uint8_t *nonce,
                                                  uint8_t *tag);
crux_credential_result_t psa_credential_port_open(const uint8_t *aad, size_t aad_length,
                                                  const uint8_t *sealed, size_t length,
                                                  const uint8_t *nonce, const uint8_t *tag,
                                                  uint8_t *plain);
crux_credential_result_t psa_credential_port_generate_key(crux_credential_key_type_t type,
                                                          uint8_t *public_key,
                                                          size_t public_capacity,
                                                          uint8_t *private_key,
                                                          size_t private_capacity, size_t *lengths);
crux_credential_result_t psa_credential_port_random(uint8_t *buffer, size_t length);

#define WRAP(name) __wrap_##name
#define REAL(name) __real_##name

crux_credential_result_t REAL(crux_credential_port_seal)(const uint8_t *aad, size_t aad_length,
                                                         const uint8_t *plain, size_t length,
                                                         uint8_t *sealed, const uint8_t *nonce,
                                                         uint8_t *tag);
crux_credential_result_t
    REAL(crux_credential_port_generate_key)(crux_credential_key_type_t type, uint8_t *public_key,
                                            size_t public_capacity, uint8_t *private_key,
                                            size_t private_capacity, size_t *lengths);
crux_credential_result_t REAL(crux_credential_port_random)(uint8_t *buffer, size_t length);
void REAL(credential_fake_open_fail)(unsigned skip, crux_credential_result_t result);
void REAL(credential_fake_device_key)(uint32_t key);
void REAL(credential_fake_reset)(void);
crux_credential_result_t WRAP(crux_credential_port_seal)(const uint8_t *aad, size_t aad_length,
                                                         const uint8_t *plain, size_t length,
                                                         uint8_t *sealed, const uint8_t *nonce,
                                                         uint8_t *tag);
crux_credential_result_t WRAP(crux_credential_port_open)(const uint8_t *aad, size_t aad_length,
                                                         const uint8_t *sealed, size_t length,
                                                         const uint8_t *nonce, const uint8_t *tag,
                                                         uint8_t *plain);
crux_credential_result_t
    WRAP(crux_credential_port_generate_key)(crux_credential_key_type_t type, uint8_t *public_key,
                                            size_t public_capacity, uint8_t *private_key,
                                            size_t private_capacity, size_t *lengths);
crux_credential_result_t WRAP(crux_credential_port_random)(uint8_t *buffer, size_t length);
void WRAP(credential_fake_open_fail)(unsigned skip, crux_credential_result_t result);
void WRAP(credential_fake_device_key)(uint32_t key);
void WRAP(credential_fake_reset)(void);

static uint32_t device_key;
static bool open_fault_armed;
static unsigned open_fault_skip;
static crux_credential_result_t open_fault_result;

size_t crux_credential_psa_device_id(uint8_t id[CRUX_CREDENTIAL_PSA_DEVICE_ID_MAX])
{
    memcpy(id, &device_key, sizeof(device_key));
    return sizeof(device_key);
}

void WRAP(credential_fake_reset)(void)
{
    device_key = 0x1178u;
    open_fault_armed = false;
    REAL(credential_fake_reset)();
}

void WRAP(credential_fake_device_key)(uint32_t key)
{
    device_key = key;
    REAL(credential_fake_device_key)(key);
}

void WRAP(credential_fake_open_fail)(unsigned skip, crux_credential_result_t result)
{
    open_fault_armed = true;
    open_fault_skip = skip;
    open_fault_result = result;
}

crux_credential_result_t WRAP(crux_credential_port_seal)(const uint8_t *aad, size_t aad_length,
                                                         const uint8_t *plain, size_t length,
                                                         uint8_t *sealed, const uint8_t *nonce,
                                                         uint8_t *tag)
{
    crux_credential_result_t result =
        REAL(crux_credential_port_seal)(aad, aad_length, plain, length, sealed, nonce, tag);
    return result != CRUX_CREDENTIAL_OK
               ? result
               : psa_credential_port_seal(aad, aad_length, plain, length, sealed, nonce, tag);
}

crux_credential_result_t WRAP(crux_credential_port_open)(const uint8_t *aad, size_t aad_length,
                                                         const uint8_t *sealed, size_t length,
                                                         const uint8_t *nonce, const uint8_t *tag,
                                                         uint8_t *plain)
{
    if (open_fault_armed) {
        if (open_fault_skip == 0u) {
            open_fault_armed = false;
            return open_fault_result;
        }
        open_fault_skip -= 1u;
    }
    return psa_credential_port_open(aad, aad_length, sealed, length, nonce, tag, plain);
}

crux_credential_result_t
WRAP(crux_credential_port_generate_key)(crux_credential_key_type_t type, uint8_t *public_key,
                                        size_t public_capacity, uint8_t *private_key,
                                        size_t private_capacity, size_t *lengths)
{
    crux_credential_result_t result = REAL(crux_credential_port_generate_key)(
        type, public_key, public_capacity, private_key, private_capacity, lengths);
    if (result != CRUX_CREDENTIAL_OK || type == CRUX_CREDENTIAL_KEY_ED25519) {
        return result;
    }
    /* The fake's lengths stay, because a test can make them wrong on purpose. */
    size_t real_lengths[2];
    return psa_credential_port_generate_key(type, public_key, public_capacity, private_key,
                                            private_capacity, real_lengths);
}

crux_credential_result_t WRAP(crux_credential_port_random)(uint8_t *buffer, size_t length)
{
    crux_credential_result_t result = REAL(crux_credential_port_random)(buffer, length);
    return result != CRUX_CREDENTIAL_OK ? result : psa_credential_port_random(buffer, length);
}
