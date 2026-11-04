/* The slot format, which the store and the tests share. Nothing here touches storage or a lock. */
#ifndef CRUX_CREDENTIAL_INTERNAL_H
#define CRUX_CREDENTIAL_INTERNAL_H

#include "credential_port.h"

#include <stdbool.h>

/* Room for the payload in a slot, once the header and tag are taken out. */
#define CRUX_CREDENTIAL_PAYLOAD_BYTES                                                              \
    (CRUX_CREDENTIAL_SLOT_BYTES - CRUX_CREDENTIAL_HEADER_BYTES - CRUX_CREDENTIAL_TAG_BYTES)

#define CRUX_CREDENTIAL_FORMAT 1u
/* One entry per kind of record. */
#define CRUX_CREDENTIAL_RECORD_KINDS 4u
#define CRUX_CREDENTIAL_TABLE_HEADER_BYTES 4u
#define CRUX_CREDENTIAL_TABLE_ENTRY_BYTES 8u
#define CRUX_CREDENTIAL_KEY_HEADER_BYTES 4u

typedef struct {
    uint16_t format;
    uint16_t protection;
    uint64_t generation;
    uint32_t payload_length;
    uint8_t nonce[CRUX_CREDENTIAL_NONCE_BYTES];
} crux_credential_header_t;

typedef enum {
    CRUX_CREDENTIAL_HEADER_VALID,
    CRUX_CREDENTIAL_HEADER_PENDING,
    CRUX_CREDENTIAL_HEADER_CORRUPT
} crux_credential_header_class_t;

void crux_credential_header_encode(const crux_credential_header_t *header,
                                   uint8_t bytes[CRUX_CREDENTIAL_HEADER_BYTES]);

/* The header of a slot that has not been finished: its magic, and the rest zero. */
void crux_credential_header_encode_pending(uint8_t bytes[CRUX_CREDENTIAL_HEADER_BYTES]);

/*
 * Classifies a header read from a slot of file_bytes bytes, at most the slot size. Anything but an
 * exact pending header or a header the store could have written is CORRUPT, so a rotted field can
 * never read as absent. header is written only for VALID.
 */
crux_credential_header_class_t
crux_credential_header_decode(const uint8_t bytes[CRUX_CREDENTIAL_HEADER_BYTES],
                              uint16_t protection, size_t file_bytes,
                              crux_credential_header_t *header);

/* Whether a record identifier is one of the four kinds. */
bool crux_credential_record_known(uint32_t record);

/* The largest size a record of this kind can have. */
size_t crux_credential_record_limit(crux_credential_record_t record);

/* An empty table: the payload of a store that holds nothing yet. Returns its length. */
size_t crux_credential_payload_empty(uint8_t *payload);

/*
 * Checks a payload that opened: known records, no repeats, and lengths that add up exactly. A tag
 * proves who wrote it, not that it is well formed. records receives one bit per record, 1 <<
 * record.
 */
bool crux_credential_payload_valid(const uint8_t *payload, size_t length, uint32_t *records);

/* Finds a record in a valid payload. */
bool crux_credential_payload_find(const uint8_t *payload, crux_credential_record_t record,
                                  const uint8_t **data, size_t *data_length);

/*
 * Adds or replaces a record in a valid payload. Returns false, changing nothing, if it would not
 * fit capacity. data must not point into the payload.
 */
bool crux_credential_payload_set(crux_credential_record_t record, const uint8_t *data,
                                 size_t data_length, uint8_t *payload, size_t *length,
                                 size_t capacity);

#endif
