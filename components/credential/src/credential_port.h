#ifndef CRUX_CREDENTIAL_PORT_H
#define CRUX_CREDENTIAL_PORT_H

#include <crux_credential_adapter.h>

/* A build setting. Each slot file has this size, a whole number of 512-byte sectors, and the store
 * holds two static buffers of about this size. A build that raises it allocates when it next writes
 * each slot, and a build that lowers it cannot save an image that no longer fits. */
#ifndef CRUX_CREDENTIAL_SLOT_BYTES
#define CRUX_CREDENTIAL_SLOT_BYTES 16384u
#endif

#define CRUX_CREDENTIAL_NONCE_BYTES 12u
#define CRUX_CREDENTIAL_TAG_BYTES 16u
/* Magic, format, protection identifier, generation, payload length and nonce. */
#define CRUX_CREDENTIAL_HEADER_BYTES 32u

/* Identifiers come from credential.* under Issue #182, which finalises them. */
typedef enum {
    CRUX_CREDENTIAL_EVENT_STATE,         /* credential.state, with the state. */
    CRUX_CREDENTIAL_EVENT_SLOT_INVALID,  /* credential.slot_invalid, with the slot. */
    CRUX_CREDENTIAL_EVENT_SAVE_FAILED,   /* credential.save_failed. */
    CRUX_CREDENTIAL_EVENT_KEY_GENERATED, /* credential.key_generated, with the record. */
    CRUX_CREDENTIAL_EVENT_KEY_REPLACED,  /* credential.key_replaced, with the record. */
    CRUX_CREDENTIAL_EVENT_ERASED         /* credential.erased, with the result. */
} crux_credential_event_type_t;

/* Holds no record bytes, so no event can carry a secret. */
typedef struct {
    crux_credential_event_type_t type;
    crux_credential_result_t result;
    crux_credential_state_t state;
    char slot;                       /* 'a' or 'b', or zero. */
    crux_credential_record_t record; /* Zero when no record applies. */
} crux_credential_event_t;

/*
 * Private provider contract, implemented by a test composition or a product provider. Every call
 * comes from thread context under the store lock, so a provider may wait, and must not call back
 * into an operation that takes that lock. No call holds the short lock behind
 * crux_credential_store_get_info(), so an event may read the state.
 */

/*
 * What protects the store. NULL means that the provider cannot run, for instance because the device
 * ID is unavailable, and the store is then UNUSABLE. A device-ID provider never falls back to a
 * fixed constant. The basis is also the image's protection identifier, so an image sealed by
 * another kind of provider reads as corrupt. A product build refuses BASIS_NONE and
 * EVIDENCE_HOST_FAKE.
 */
const crux_credential_store_info_t *crux_credential_port_declaration(void);

/*
 * Seal one image. aad holds the whole header, including the nonce. plain and sealed do not overlap.
 * Returns OK, ENTROPY if the provider's own randomness failed, or another error otherwise.
 */
crux_credential_result_t crux_credential_port_seal(const uint8_t *aad, size_t aad_length,
                                                   const uint8_t *plain, size_t length,
                                                   uint8_t *sealed,
                                                   const uint8_t nonce[CRUX_CREDENTIAL_NONCE_BYTES],
                                                   uint8_t tag[CRUX_CREDENTIAL_TAG_BYTES]);

/*
 * Open one image. Returns OK, ERR_UNUSABLE when the tag does not verify, which marks the slot
 * corrupt, or another error when the provider could not run, which marks it unreadable: that proves
 * nothing about which generation is newest. plain and sealed do not overlap.
 */
crux_credential_result_t crux_credential_port_open(const uint8_t *aad, size_t aad_length,
                                                   const uint8_t *sealed, size_t length,
                                                   const uint8_t nonce[CRUX_CREDENTIAL_NONCE_BYTES],
                                                   const uint8_t tag[CRUX_CREDENTIAL_TAG_BYTES],
                                                   uint8_t *plain);

/*
 * Generate a key pair. lengths receives the public, then the private length, which must equal the
 * sizes the adapter header states for the type. Any failure is reported as ERR_ENTROPY.
 */
crux_credential_result_t
crux_credential_port_generate_key(crux_credential_key_type_t type, uint8_t *public_key,
                                  size_t public_capacity, uint8_t *private_key,
                                  size_t private_capacity, size_t lengths[2]);

/* Random bytes for nonces. Returns OK or ERR_ENTROPY. */
crux_credential_result_t crux_credential_port_random(uint8_t *buffer, size_t length);

/* Reports an event without waiting on storage. */
void crux_credential_port_event(const crux_credential_event_t *event);

#endif
