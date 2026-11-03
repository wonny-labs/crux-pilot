#include "access_internal.h"

#include <string.h>

/*
 * Record version 1, little-endian:
 *   u16 version, u8 account count, u8 reserved
 *   each account: name[32], role[32], u8 flags, u8 algorithm, u8 key count,
 *                 u32 iterations, salt, derived, then for each key: u16 length, bytes
 * Integrity across power loss belongs to the store. This layer checks only that a record it was
 * told is valid has the shape it can use, and refuses it otherwise.
 */
#define ACCESS_RECORD_VERSION 1u
#define ACCESS_FLAG_ENABLED 0x01u
#define ACCESS_FLAG_MUST_CHANGE 0x02u
#define ACCESS_FLAGS_KNOWN (ACCESS_FLAG_ENABLED | ACCESS_FLAG_MUST_CHANGE)

typedef struct {
    uint8_t *bytes;
    size_t capacity;
    size_t position;
    bool failed;
} writer_t;

static void put_bytes(writer_t *writer, const void *data, size_t length)
{
    if (writer->failed || length > writer->capacity - writer->position) {
        writer->failed = true;
        return;
    }
    memcpy(&writer->bytes[writer->position], data, length);
    writer->position += length;
}

static void put_u8(writer_t *writer, uint8_t value)
{
    put_bytes(writer, &value, 1);
}

static void put_u16(writer_t *writer, uint16_t value)
{
    uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    put_bytes(writer, bytes, sizeof(bytes));
}

static void put_u32(writer_t *writer, uint32_t value)
{
    uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16),
                        (uint8_t)(value >> 24)};
    put_bytes(writer, bytes, sizeof(bytes));
}

static void put_name(writer_t *writer, const char *name)
{
    char padded[CRUX_ACCESS_NAME_SIZE] = {0};
    crux_access_copy_name(padded, name);
    put_bytes(writer, padded, sizeof(padded));
}

size_t crux_access_record_encode(const access_table_t *table, uint8_t *record, size_t capacity)
{
    writer_t writer = {record, capacity, 0, false};
    put_u16(&writer, ACCESS_RECORD_VERSION);
    put_u8(&writer, (uint8_t)table->count);
    put_u8(&writer, 0);
    for (size_t index = 0; index < table->count; ++index) {
        const access_account_t *account = &table->accounts[index];
        uint8_t flags = (uint8_t)((account->enabled ? ACCESS_FLAG_ENABLED : 0u) |
                                  (account->must_change_password ? ACCESS_FLAG_MUST_CHANGE : 0u));
        put_name(&writer, account->user);
        put_name(&writer, account->role);
        put_u8(&writer, flags);
        put_u8(&writer, (uint8_t)account->algorithm);
        put_u8(&writer, (uint8_t)account->key_count);
        put_u32(&writer, account->iterations);
        put_bytes(&writer, account->salt, sizeof(account->salt));
        put_bytes(&writer, account->derived, sizeof(account->derived));
        for (size_t key = 0; key < account->key_count; ++key) {
            put_u16(&writer, (uint16_t)account->keys[key].length);
            put_bytes(&writer, account->keys[key].bytes, account->keys[key].length);
        }
    }
    return writer.failed ? 0u : writer.position;
}

typedef struct {
    const uint8_t *bytes;
    size_t length;
    size_t position;
} reader_t;

static bool get_bytes(reader_t *reader, void *output, size_t length)
{
    if (length > reader->length - reader->position) {
        return false;
    }
    memcpy(output, &reader->bytes[reader->position], length);
    reader->position += length;
    return true;
}

static bool get_u8(reader_t *reader, uint8_t *value)
{
    return get_bytes(reader, value, 1);
}

static bool get_u16(reader_t *reader, uint16_t *value)
{
    uint8_t bytes[2];
    if (!get_bytes(reader, bytes, sizeof(bytes))) {
        return false;
    }
    *value = (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
    return true;
}

static bool get_u32(reader_t *reader, uint32_t *value)
{
    uint8_t bytes[4];
    if (!get_bytes(reader, bytes, sizeof(bytes))) {
        return false;
    }
    *value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
             ((uint32_t)bytes[3] << 24);
    return true;
}

static bool padded_name_valid(const char name[CRUX_ACCESS_NAME_SIZE])
{
    size_t length = crux_access_bounded_length(name, CRUX_ACCESS_NAME_SIZE - 1u);
    if (length == 0 || length >= CRUX_ACCESS_NAME_SIZE) {
        return false;
    }
    for (size_t index = length; index < CRUX_ACCESS_NAME_SIZE; ++index) {
        if (name[index] != '\0') {
            return false;
        }
    }
    return true;
}

/* One SSH string: a big-endian 32-bit length, then that many bytes. */
static bool ssh_string(reader_t *reader, const uint8_t **text, size_t *length)
{
    uint8_t bytes[4];
    if (!get_bytes(reader, bytes, sizeof(bytes))) {
        return false;
    }
    uint32_t claimed = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
                       ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
    if (claimed > reader->length - reader->position) {
        return false;
    }
    *text = &reader->bytes[reader->position];
    *length = claimed;
    reader->position += claimed;
    return true;
}

static bool ssh_string_is(const uint8_t *text, size_t length, const char *expected)
{
    return length == strlen(expected) && memcmp(text, expected, length) == 0;
}

typedef struct {
    const char *type;
    const char *curve; /* NULL for a type with no curve string. */
    size_t point_size;
} key_type_t;

static const key_type_t key_types[] = {
    {"ssh-ed25519", NULL, 32u},
    {"ecdsa-sha2-nistp256", "nistp256", 65u},
    {"ecdsa-sha2-nistp384", "nistp384", 97u},
    {"ecdsa-sha2-nistp521", "nistp521", 133u},
};

#define ACCESS_EC_POINT_UNCOMPRESSED 0x04u

bool crux_access_key_acceptable(const uint8_t *key, size_t length)
{
    if (length == 0 || length > CRUX_ACCESS_KEY_SIZE_MAX) {
        return false;
    }
    reader_t reader = {key, length, 0};
    const uint8_t *text = NULL;
    size_t text_length = 0;
    if (!ssh_string(&reader, &text, &text_length)) {
        return false;
    }
    const key_type_t *type = NULL;
    for (size_t index = 0; index < sizeof(key_types) / sizeof(key_types[0]); ++index) {
        if (ssh_string_is(text, text_length, key_types[index].type)) {
            type = &key_types[index];
        }
    }
    if (type == NULL) {
        return false;
    }
    if (type->curve != NULL && (!ssh_string(&reader, &text, &text_length) ||
                                !ssh_string_is(text, text_length, type->curve))) {
        return false;
    }
    if (!ssh_string(&reader, &text, &text_length) || text_length != type->point_size) {
        return false;
    }
    if (type->curve != NULL && text[0] != ACCESS_EC_POINT_UNCOMPRESSED) {
        return false;
    }
    return reader.position == length;
}

static bool decode_key(reader_t *reader, access_key_t *key)
{
    uint16_t length = 0;
    if (!get_u16(reader, &length) || length == 0 || length > CRUX_ACCESS_KEY_SIZE_MAX) {
        return false;
    }
    key->length = length;
    return get_bytes(reader, key->bytes, length) && crux_access_key_acceptable(key->bytes, length);
}

static bool decode_account(reader_t *reader, access_account_t *account)
{
    uint8_t flags = 0, algorithm = 0, key_count = 0;
    if (!get_bytes(reader, account->user, CRUX_ACCESS_NAME_SIZE) ||
        !get_bytes(reader, account->role, CRUX_ACCESS_NAME_SIZE) || !get_u8(reader, &flags) ||
        !get_u8(reader, &algorithm) || !get_u8(reader, &key_count) ||
        !get_u32(reader, &account->iterations) ||
        !get_bytes(reader, account->salt, sizeof(account->salt)) ||
        !get_bytes(reader, account->derived, sizeof(account->derived))) {
        return false;
    }
    if (!padded_name_valid(account->user) || !crux_access_name_valid(account->user) ||
        !padded_name_valid(account->role) || (flags & ~ACCESS_FLAGS_KNOWN) != 0 ||
        algorithm != CRUX_ACCESS_VERIFIER_PBKDF2_HMAC_SHA256 || account->iterations == 0 ||
        key_count > CRUX_ACCESS_KEYS_PER_ACCOUNT) {
        return false;
    }
    account->enabled = (flags & ACCESS_FLAG_ENABLED) != 0;
    account->must_change_password = (flags & ACCESS_FLAG_MUST_CHANGE) != 0;
    account->algorithm = algorithm;
    account->key_count = key_count;
    for (size_t key = 0; key < key_count; ++key) {
        if (!decode_key(reader, &account->keys[key])) {
            return false;
        }
    }
    return true;
}

bool crux_access_record_decode(const uint8_t *record, size_t length, access_table_t *table)
{
    reader_t reader = {record, length, 0};
    uint16_t version = 0;
    uint8_t count = 0, reserved = 0;
    if (!get_u16(&reader, &version) || !get_u8(&reader, &count) || !get_u8(&reader, &reserved) ||
        version != ACCESS_RECORD_VERSION || reserved != 0 || count == 0 ||
        count > CRUX_ACCESS_ACCOUNT_MAX) {
        return false;
    }
    memset(table, 0, sizeof(*table));
    bool valid = true;
    for (size_t index = 0; valid && index < count; ++index) {
        access_account_t *account = &table->accounts[index];
        table->count = index;
        valid = decode_account(&reader, account) &&
                crux_access_table_find(table, account->user, NULL) == NULL;
    }
    valid = valid && reader.position == length;
    if (!valid) {
        crux_access_secure_zero(table, sizeof(*table));
        return false;
    }
    table->count = count;
    return true;
}
