#include "credential_internal.h"

#include <string.h>

#define MAGIC_BYTES 4u

#define OFFSET_FORMAT 4u
#define OFFSET_PROTECTION 6u
#define OFFSET_GENERATION 8u
#define OFFSET_PAYLOAD_LENGTH 16u
#define OFFSET_NONCE 20u

_Static_assert(OFFSET_NONCE + CRUX_CREDENTIAL_NONCE_BYTES == CRUX_CREDENTIAL_HEADER_BYTES,
               "The header's fields must fill it exactly");

static const uint8_t magic_valid[MAGIC_BYTES] = {'C', 'X', 'C', 'S'};
static const uint8_t magic_pending[MAGIC_BYTES] = {'C', 'X', 'C', 'P'};

/* One entry of a payload's table. */
typedef struct {
    uint16_t id;
    uint32_t length;
} table_entry_t;

static void put_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *bytes, uint32_t value)
{
    put_u16(bytes, (uint16_t)value);
    put_u16(bytes + 2, (uint16_t)(value >> 16));
}

static void put_u64(uint8_t *bytes, uint64_t value)
{
    put_u32(bytes, (uint32_t)value);
    put_u32(bytes + 4, (uint32_t)(value >> 32));
}

static uint16_t get_u16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t get_u32(const uint8_t *bytes)
{
    return (uint32_t)get_u16(bytes) | ((uint32_t)get_u16(bytes + 2) << 16);
}

static uint64_t get_u64(const uint8_t *bytes)
{
    return (uint64_t)get_u32(bytes) | ((uint64_t)get_u32(bytes + 4) << 32);
}

void crux_credential_header_encode(const crux_credential_header_t *header,
                                   uint8_t bytes[CRUX_CREDENTIAL_HEADER_BYTES])
{
    memcpy(bytes, magic_valid, MAGIC_BYTES);
    put_u16(bytes + OFFSET_FORMAT, header->format);
    put_u16(bytes + OFFSET_PROTECTION, header->protection);
    put_u64(bytes + OFFSET_GENERATION, header->generation);
    put_u32(bytes + OFFSET_PAYLOAD_LENGTH, header->payload_length);
    memcpy(bytes + OFFSET_NONCE, header->nonce, CRUX_CREDENTIAL_NONCE_BYTES);
}

void crux_credential_header_encode_pending(uint8_t bytes[CRUX_CREDENTIAL_HEADER_BYTES])
{
    memset(bytes, 0, CRUX_CREDENTIAL_HEADER_BYTES);
    memcpy(bytes, magic_pending, MAGIC_BYTES);
}

crux_credential_header_class_t
crux_credential_header_decode(const uint8_t bytes[CRUX_CREDENTIAL_HEADER_BYTES],
                              uint16_t protection, size_t file_bytes,
                              crux_credential_header_t *header)
{
    if (memcmp(bytes, magic_pending, MAGIC_BYTES) == 0) {
        uint8_t difference = 0;
        for (size_t index = MAGIC_BYTES; index < CRUX_CREDENTIAL_HEADER_BYTES; ++index) {
            difference = (uint8_t)(difference | bytes[index]);
        }
        return difference == 0 ? CRUX_CREDENTIAL_HEADER_PENDING : CRUX_CREDENTIAL_HEADER_CORRUPT;
    }
    if (memcmp(bytes, magic_valid, MAGIC_BYTES) != 0) {
        return CRUX_CREDENTIAL_HEADER_CORRUPT;
    }
    crux_credential_header_t decoded = {
        .format = get_u16(bytes + OFFSET_FORMAT),
        .protection = get_u16(bytes + OFFSET_PROTECTION),
        .generation = get_u64(bytes + OFFSET_GENERATION),
        .payload_length = get_u32(bytes + OFFSET_PAYLOAD_LENGTH),
    };
    memcpy(decoded.nonce, bytes + OFFSET_NONCE, CRUX_CREDENTIAL_NONCE_BYTES);
    if (decoded.format != CRUX_CREDENTIAL_FORMAT || decoded.protection != protection ||
        decoded.payload_length > CRUX_CREDENTIAL_PAYLOAD_BYTES ||
        file_bytes < CRUX_CREDENTIAL_HEADER_BYTES + CRUX_CREDENTIAL_TAG_BYTES ||
        decoded.payload_length >
            file_bytes - CRUX_CREDENTIAL_HEADER_BYTES - CRUX_CREDENTIAL_TAG_BYTES) {
        return CRUX_CREDENTIAL_HEADER_CORRUPT;
    }
    *header = decoded;
    return CRUX_CREDENTIAL_HEADER_VALID;
}

bool crux_credential_record_known(uint32_t record)
{
    return record == CRUX_CREDENTIAL_RECORD_ACCOUNTS || record == CRUX_CREDENTIAL_RECORD_TLS_KEY ||
           record == CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE ||
           record == CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY;
}

size_t crux_credential_record_limit(crux_credential_record_t record)
{
    switch (record) {
    case CRUX_CREDENTIAL_RECORD_ACCOUNTS:
        return CRUX_CREDENTIAL_ACCOUNTS_BYTES_MAX;
    case CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE:
        return CRUX_CREDENTIAL_CERTIFICATE_BYTES_MAX;
    case CRUX_CREDENTIAL_RECORD_TLS_KEY:
    case CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY:
        return CRUX_CREDENTIAL_KEY_RECORD_BYTES_MAX;
    }
    return 0;
}

size_t crux_credential_payload_empty(uint8_t *payload)
{
    memset(payload, 0, CRUX_CREDENTIAL_TABLE_HEADER_BYTES);
    return CRUX_CREDENTIAL_TABLE_HEADER_BYTES;
}

static size_t table_bytes(size_t count)
{
    return CRUX_CREDENTIAL_TABLE_HEADER_BYTES + count * CRUX_CREDENTIAL_TABLE_ENTRY_BYTES;
}

static uint16_t entry_id(const uint8_t *payload, size_t index)
{
    return get_u16(payload + table_bytes(index));
}

static uint32_t entry_length(const uint8_t *payload, size_t index)
{
    return get_u32(payload + table_bytes(index) + 4u);
}

static void entry_put(uint8_t *payload, size_t index, table_entry_t entry)
{
    uint8_t *bytes = payload + table_bytes(index);
    put_u16(bytes, entry.id);
    put_u16(bytes + 2u, 0);
    put_u32(bytes + 4u, entry.length);
}

bool crux_credential_payload_valid(const uint8_t *payload, size_t length, uint32_t *records)
{
    if (length < CRUX_CREDENTIAL_TABLE_HEADER_BYTES) {
        return false;
    }
    size_t count = get_u16(payload);
    if (get_u16(payload + 2u) != 0 || count > CRUX_CREDENTIAL_RECORD_KINDS ||
        length < table_bytes(count)) {
        return false;
    }
    size_t remaining = length - table_bytes(count);
    uint32_t seen = 0;
    for (size_t index = 0; index < count; ++index) {
        uint32_t id = entry_id(payload, index);
        uint32_t record_length = entry_length(payload, index);
        if (!crux_credential_record_known(id) || (seen & (1u << id)) != 0 ||
            get_u16(payload + table_bytes(index) + 2u) != 0 || record_length == 0 ||
            record_length > remaining) {
            return false;
        }
        seen |= 1u << id;
        remaining -= record_length;
    }
    if (remaining != 0) {
        return false;
    }
    *records = seen;
    return true;
}

bool crux_credential_payload_find(const uint8_t *payload, crux_credential_record_t record,
                                  const uint8_t **data, size_t *data_length)
{
    size_t count = get_u16(payload);
    size_t offset = table_bytes(count);
    for (size_t index = 0; index < count; ++index) {
        size_t record_length = entry_length(payload, index);
        if (entry_id(payload, index) == (uint16_t)record) {
            *data = payload + offset;
            *data_length = record_length;
            return true;
        }
        offset += record_length;
    }
    return false;
}

bool crux_credential_payload_set(crux_credential_record_t record, const uint8_t *data,
                                 size_t data_length, uint8_t *payload, size_t *length,
                                 size_t capacity)
{
    size_t count = get_u16(payload);
    table_entry_t entries[CRUX_CREDENTIAL_RECORD_KINDS] = {{0, 0}};
    size_t offset = table_bytes(count);
    size_t old_offset = 0;
    size_t old_length = 0;
    size_t kept = 0;
    bool found = false;
    for (size_t index = 0; index < count; ++index) {
        table_entry_t entry = {entry_id(payload, index), entry_length(payload, index)};
        if (entry.id == (uint16_t)record) {
            found = true;
            old_offset = offset;
            old_length = entry.length;
        } else {
            entries[kept] = entry;
            kept += 1;
        }
        offset += entry.length;
    }
    size_t new_count = kept + 1u;
    size_t records_bytes = *length - table_bytes(count) - old_length;
    size_t new_length = table_bytes(new_count) + records_bytes + data_length;
    if (new_length > capacity) {
        return false;
    }
    /* Close the old record's gap, then move the remaining records to follow the new table. */
    if (found) {
        memmove(payload + old_offset, payload + old_offset + old_length,
                *length - old_offset - old_length);
    }
    memmove(payload + table_bytes(new_count), payload + table_bytes(count), records_bytes);
    memcpy(payload + table_bytes(new_count) + records_bytes, data, data_length);
    entries[kept] = (table_entry_t){(uint16_t)record, (uint32_t)data_length};
    put_u16(payload, (uint16_t)new_count);
    put_u16(payload + 2u, 0);
    for (size_t index = 0; index < new_count; ++index) {
        entry_put(payload, index, entries[index]);
    }
    *length = new_length;
    return true;
}

typedef struct {
    size_t public_length;
    size_t private_length;
} key_sizes_t;

static bool key_sizes(uint32_t type, key_sizes_t *sizes)
{
    switch (type) {
    case CRUX_CREDENTIAL_KEY_ECDSA_P256:
        *sizes = (key_sizes_t){65u, 32u};
        return true;
    case CRUX_CREDENTIAL_KEY_ED25519:
        *sizes = (key_sizes_t){32u, 32u};
        return true;
    default:
        return false;
    }
}

_Static_assert(CRUX_CREDENTIAL_KEY_HEADER_BYTES + 65u + 32u == CRUX_CREDENTIAL_KEY_RECORD_BYTES_MAX,
               "The key record limit must hold the largest key");
_Static_assert(CRUX_CREDENTIAL_KEY_PUBLIC_BYTES_MAX == 65u,
               "The public key limit must hold the largest key");

crux_credential_result_t crux_credential_key_parse(const void *record, size_t length,
                                                   crux_credential_key_view_t *view)
{
    if (record == NULL || view == NULL || length < CRUX_CREDENTIAL_KEY_HEADER_BYTES) {
        return CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT;
    }
    const uint8_t *bytes = record;
    key_sizes_t sizes;
    if (!key_sizes(bytes[0], &sizes) || bytes[1] != sizes.public_length ||
        bytes[2] != sizes.private_length || bytes[3] != 0 ||
        length != CRUX_CREDENTIAL_KEY_HEADER_BYTES + sizes.public_length + sizes.private_length) {
        return CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT;
    }
    *view = (crux_credential_key_view_t){
        .type = (crux_credential_key_type_t)bytes[0],
        .public_key = bytes + CRUX_CREDENTIAL_KEY_HEADER_BYTES,
        .public_length = sizes.public_length,
        .private_key = bytes + CRUX_CREDENTIAL_KEY_HEADER_BYTES + sizes.public_length,
        .private_length = sizes.private_length,
    };
    return CRUX_CREDENTIAL_OK;
}
