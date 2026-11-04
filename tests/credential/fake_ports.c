#ifndef CRUX_CREDENTIAL_TEST_COMPOSITION
#error                                                                                             \
    "The fake credential ports are a test composition and must not be selected for a product image"
#endif

#include "fake_ports.h"

#include <stdlib.h>
#include <string.h>

#define FAKE_EVENTS_MAX 4096u
#define FAKE_NONCES_MAX 4096u

typedef struct {
    bool armed;
    unsigned skip;
    crux_credential_result_t result;
} fault_t;

static const crux_credential_store_info_t default_declaration = {
    .state = CRUX_CREDENTIAL_STATE_UNUSABLE,
    .basis = CRUX_CREDENTIAL_BASIS_NONE,
    .evidence = CRUX_CREDENTIAL_EVIDENCE_HOST_FAKE,
    .confidential_against_medium_reader = false,
    .forgery_detected_without_device_id = false,
    .rollback_detected = false,
};

static uint32_t device_key;
static const crux_credential_store_info_t *declared;
static uint32_t random_state;
static fault_t random_fault, seal_fault, open_fault;
static bool keygen_fails, keygen_bad;
static unsigned keygen_count;
static uint8_t nonces[FAKE_NONCES_MAX][CRUX_CREDENTIAL_NONCE_BYTES];
static size_t nonce_count;
static crux_credential_event_t events[FAKE_EVENTS_MAX];
static size_t event_count;
static void (*event_hook)(const crux_credential_event_t *event);

void credential_fake_reset(void)
{
    device_key = 0x1178u;
    declared = &default_declaration;
    random_state = 0x2024u;
    random_fault = (fault_t){0};
    seal_fault = (fault_t){0};
    open_fault = (fault_t){0};
    keygen_fails = keygen_bad = false;
    keygen_count = 0;
    nonce_count = 0;
    event_count = 0;
    event_hook = NULL;
}

void credential_fake_device_key(uint32_t key)
{
    device_key = key;
}

void credential_fake_declare(const crux_credential_store_info_t *declaration)
{
    declared = declaration;
}

const crux_credential_store_info_t *credential_fake_default_declaration(void)
{
    return &default_declaration;
}

static void fault_arm(fault_t *fault, unsigned skip, crux_credential_result_t result)
{
    *fault = (fault_t){.armed = true, .skip = skip, .result = result};
}

static bool fault_hit(fault_t *fault)
{
    if (!fault->armed) {
        return false;
    }
    if (fault->skip > 0) {
        fault->skip -= 1;
        return false;
    }
    fault->armed = false;
    return true;
}

void credential_fake_random_fail(unsigned skip)
{
    fault_arm(&random_fault, skip, CRUX_CREDENTIAL_ERR_ENTROPY);
}

void credential_fake_seal_fail(unsigned skip, crux_credential_result_t result)
{
    fault_arm(&seal_fault, skip, result);
}

void credential_fake_open_fail(unsigned skip, crux_credential_result_t result)
{
    fault_arm(&open_fault, skip, result);
}

void credential_fake_keygen_fail(bool fail)
{
    keygen_fails = fail;
}

void credential_fake_keygen_bad_lengths(bool bad)
{
    keygen_bad = bad;
}

unsigned credential_fake_keygen_count(void)
{
    return keygen_count;
}

size_t credential_fake_nonce_count(void)
{
    return nonce_count;
}

const uint8_t *credential_fake_nonce(size_t index)
{
    return nonces[index];
}

size_t credential_fake_event_count(void)
{
    return event_count;
}

const crux_credential_event_t *credential_fake_event(size_t index)
{
    return index < event_count ? &events[index] : NULL;
}

size_t credential_fake_event_count_of(crux_credential_event_type_t type)
{
    size_t count = 0;
    for (size_t index = 0; index < event_count; ++index) {
        count += events[index].type == type ? 1u : 0u;
    }
    return count;
}

size_t credential_fake_slot_invalid_count(char slot)
{
    size_t count = 0;
    for (size_t index = 0; index < event_count; ++index) {
        count +=
            events[index].type == CRUX_CREDENTIAL_EVENT_SLOT_INVALID && events[index].slot == slot
                ? 1u
                : 0u;
    }
    return count;
}

void credential_fake_events_clear(void)
{
    event_count = 0;
}

void credential_fake_event_hook(void (*hook)(const crux_credential_event_t *event))
{
    event_hook = hook;
}

const crux_credential_store_info_t *crux_credential_port_declaration(void)
{
    return declared;
}

/* Deliberately not cryptography. It keeps plain bytes off the medium and depends on key and nonce.
 */
static void fake_cipher(const uint8_t *input, size_t length, const uint8_t *nonce, uint8_t *output)
{
    uint32_t state = device_key;
    for (size_t index = 0; index < CRUX_CREDENTIAL_NONCE_BYTES; ++index) {
        state = (state ^ nonce[index]) * 16777619u;
    }
    for (size_t index = 0; index < length; ++index) {
        state = state * 1664525u + 1013904223u;
        output[index] = (uint8_t)(input[index] ^ (uint8_t)(state >> 24));
    }
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    static uint32_t table[256];
    if (table[1] == 0) {
        for (uint32_t value = 0; value < 256u; ++value) {
            uint32_t entry = value;
            for (unsigned bit = 0; bit < 8u; ++bit) {
                entry = (entry & 1u) != 0 ? (entry >> 1) ^ 0xEDB88320u : entry >> 1;
            }
            table[value] = entry;
        }
    }
    for (size_t index = 0; index < length; ++index) {
        crc = table[(crc ^ data[index]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

static void fake_tag(const uint8_t *aad, size_t aad_length, const uint8_t *sealed, size_t length,
                     const uint8_t *nonce, uint8_t tag[CRUX_CREDENTIAL_TAG_BYTES])
{
    const uint8_t key_bytes[4] = {(uint8_t)device_key, (uint8_t)(device_key >> 8),
                                  (uint8_t)(device_key >> 16), (uint8_t)(device_key >> 24)};
    uint32_t crc = 0xFFFFFFFFu;
    crc = crc32_update(crc, key_bytes, sizeof(key_bytes));
    crc = crc32_update(crc, aad, aad_length);
    crc = crc32_update(crc, nonce, CRUX_CREDENTIAL_NONCE_BYTES);
    crc = crc32_update(crc, sealed, length);
    for (size_t index = 0; index < CRUX_CREDENTIAL_TAG_BYTES; ++index) {
        tag[index] = (uint8_t)((crc >> (8u * (index % 4u))) ^ (uint8_t)index);
    }
}

crux_credential_result_t crux_credential_port_seal(const uint8_t *aad, size_t aad_length,
                                                   const uint8_t *plain, size_t length,
                                                   uint8_t *sealed,
                                                   const uint8_t nonce[CRUX_CREDENTIAL_NONCE_BYTES],
                                                   uint8_t tag[CRUX_CREDENTIAL_TAG_BYTES])
{
    if (fault_hit(&seal_fault)) {
        return seal_fault.result;
    }
    if (nonce_count < FAKE_NONCES_MAX) {
        memcpy(nonces[nonce_count], nonce, CRUX_CREDENTIAL_NONCE_BYTES);
        nonce_count += 1;
    }
    fake_cipher(plain, length, nonce, sealed);
    fake_tag(aad, aad_length, sealed, length, nonce, tag);
    return CRUX_CREDENTIAL_OK;
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
    if (fault_hit(&open_fault)) {
        return open_fault.result;
    }
    uint8_t expected[CRUX_CREDENTIAL_TAG_BYTES];
    fake_tag(aad, aad_length, sealed, length, nonce, expected);
    if (memcmp(expected, tag, sizeof(expected)) != 0) {
        return CRUX_CREDENTIAL_ERR_UNUSABLE;
    }
    fake_cipher(sealed, length, nonce, plain);
    return CRUX_CREDENTIAL_OK;
}

crux_credential_result_t crux_credential_port_random(uint8_t *buffer, size_t length)
{
    if (fault_hit(&random_fault)) {
        return random_fault.result;
    }
    for (size_t index = 0; index < length; ++index) {
        random_state = random_state * 1664525u + 1013904223u;
        buffer[index] = (uint8_t)(random_state >> 24);
    }
    return CRUX_CREDENTIAL_OK;
}

crux_credential_result_t
crux_credential_port_generate_key(crux_credential_key_type_t type, uint8_t *public_key,
                                  size_t public_capacity, uint8_t *private_key,
                                  size_t private_capacity, size_t lengths[2])
{
    if (keygen_fails) {
        return CRUX_CREDENTIAL_ERR_ENTROPY;
    }
    size_t public_length = type == CRUX_CREDENTIAL_KEY_ECDSA_P256 ? 65u : 32u;
    size_t private_length = 32u;
    if (public_length > public_capacity || private_length > private_capacity) {
        abort();
    }
    keygen_count += 1;
    memset(public_key, (int)(0xA0u + keygen_count), public_length);
    memset(private_key, (int)(0x50u + keygen_count), private_length);
    lengths[0] = keygen_bad ? public_length - 1u : public_length;
    lengths[1] = private_length;
    return CRUX_CREDENTIAL_OK;
}

void crux_credential_port_event(const crux_credential_event_t *event)
{
    if (event_count < FAKE_EVENTS_MAX) {
        events[event_count] = *event;
        event_count += 1;
    }
    if (event_hook != NULL) {
        event_hook(event);
    }
}
