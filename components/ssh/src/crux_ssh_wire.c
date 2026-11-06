/*
 * The checks a provider applies to what a client sends, and the host key's public form. See
 * crux_ssh_wire.h. Every read is bounded by the length the caller gave, and nothing allocates.
 */
#include "crux_ssh_wire.h"

#include <string.h>

#define STRING_LENGTH_BYTES 4u
#define POINT_UNCOMPRESSED 0x04u

static const char name_ed25519[] = "ssh-ed25519";
static const char name_p256[] = "ecdsa-sha2-nistp256";
static const char curve_p256[] = "nistp256";

/* The only key forms Crux accepts, for a user key and for the host key. */
typedef struct {
    crux_ssh_key_type_t type;
    const char *name;
    const char *curve; /* NULL for a type with no curve name. */
    size_t point_length;
} key_form_t;

static const key_form_t key_forms[] = {
    {CRUX_SSH_KEY_ED25519, name_ed25519, NULL, 32u},
    {CRUX_SSH_KEY_ECDSA_P256, name_p256, curve_p256, 65u},
};

typedef struct {
    const uint8_t *bytes;
    size_t length;
    size_t position;
} reader_t;

static bool string_is(const uint8_t *text, size_t length, const char *expected)
{
    return length == strlen(expected) && memcmp(text, expected, length) == 0;
}

/* One SSH string: a big-endian 32-bit length, then that many bytes, all of which must be present.
 */
static bool read_string(reader_t *reader, const uint8_t **text, size_t *length)
{
    const uint8_t *at;
    uint32_t claimed;

    if (reader->length - reader->position < STRING_LENGTH_BYTES) {
        return false;
    }
    at = reader->bytes + reader->position;
    claimed = ((uint32_t)at[0] << 24u) | ((uint32_t)at[1] << 16u) | ((uint32_t)at[2] << 8u) |
              (uint32_t)at[3];
    reader->position += STRING_LENGTH_BYTES;
    if (claimed > reader->length - reader->position) {
        return false;
    }
    *text = reader->bytes + reader->position;
    *length = claimed;
    reader->position += claimed;
    return true;
}

static const key_form_t *form_of(crux_ssh_key_type_t type)
{
    for (size_t i = 0; i < sizeof(key_forms) / sizeof(key_forms[0]); ++i) {
        if (key_forms[i].type == type) {
            return &key_forms[i];
        }
    }
    return NULL;
}

static bool blob_has_form(const uint8_t *blob, size_t blob_length, const key_form_t *form)
{
    reader_t reader = {blob, blob_length, 0u};
    const uint8_t *text = NULL;
    size_t text_length = 0u;

    if (!read_string(&reader, &text, &text_length) || !string_is(text, text_length, form->name)) {
        return false;
    }
    if (form->curve != NULL && (!read_string(&reader, &text, &text_length) ||
                                !string_is(text, text_length, form->curve))) {
        return false;
    }
    if (!read_string(&reader, &text, &text_length) || text_length != form->point_length) {
        return false;
    }
    if (form->curve != NULL && text[0] != POINT_UNCOMPRESSED) {
        return false;
    }
    return reader.position == reader.length;
}

bool crux_ssh_wire_user_name(const uint8_t *name, size_t length,
                             char user[CRUX_SSH_USER_NAME_MAX + 1u])
{
    if (user != NULL) {
        user[0] = '\0';
    }
    if (name == NULL || user == NULL || length == 0u || length > CRUX_SSH_USER_NAME_MAX ||
        memchr(name, '\0', length) != NULL) {
        return false;
    }
    memcpy(user, name, length);
    user[length] = '\0';
    return true;
}

bool crux_ssh_wire_user_key(const uint8_t *algorithm, size_t algorithm_length, const uint8_t *blob,
                            size_t blob_length, crux_ssh_key_type_t *type)
{
    if (algorithm == NULL || blob == NULL || type == NULL) {
        return false;
    }
    for (size_t i = 0; i < sizeof(key_forms) / sizeof(key_forms[0]); ++i) {
        const key_form_t *form = &key_forms[i];

        if (!string_is(algorithm, algorithm_length, form->name)) {
            continue;
        }
        if (!blob_has_form(blob, blob_length, form)) {
            return false;
        }
        *type = form->type;
        return true;
    }
    return false;
}

/* The one string that a request's data must hold, with nothing after it. */
static const uint8_t *request_string(const uint8_t *data, size_t data_length, size_t *text_length)
{
    reader_t reader = {data, data_length, 0u};
    const uint8_t *text = NULL;

    if (data == NULL || !read_string(&reader, &text, text_length)) {
        return NULL;
    }
    return reader.position == reader.length ? text : NULL;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the order the header states
bool crux_ssh_wire_exec_command(const uint8_t *data, size_t data_length, size_t command_max,
                                const uint8_t **command, size_t *command_length)
{
    if (command == NULL || command_length == NULL) {
        return false;
    }
    *command_length = 0u;
    *command = request_string(data, data_length, command_length);
    if (*command == NULL || *command_length > command_max ||
        memchr(*command, '\0', *command_length) != NULL) {
        *command = NULL;
        *command_length = 0u;
        return false;
    }
    return true;
}

bool crux_ssh_wire_subsystem_is_sftp(const uint8_t *data, size_t data_length)
{
    size_t text_length = 0u;
    const uint8_t *text = request_string(data, data_length, &text_length);

    return text != NULL && string_is(text, text_length, "sftp");
}

bool crux_ssh_wire_host_key_form(crux_ssh_key_type_t type, const uint8_t *host_key,
                                 size_t host_key_length)
{
    if (host_key == NULL) {
        return false;
    }
    if (type == CRUX_SSH_KEY_ED25519) {
        return host_key_length == CRUX_SSH_WIRE_HOST_KEY_ED25519_BYTES;
    }
    return type == CRUX_SSH_KEY_ECDSA_P256 &&
           host_key_length == CRUX_SSH_WIRE_HOST_KEY_P256_BYTES &&
           host_key[CRUX_SSH_WIRE_HOST_KEY_PRIVATE_BYTES] == POINT_UNCOMPRESSED;
}

static uint8_t *put_string(uint8_t *at, const void *text, size_t length)
{
    at[0] = (uint8_t)(length >> 24u);
    at[1] = (uint8_t)(length >> 16u);
    at[2] = (uint8_t)(length >> 8u);
    at[3] = (uint8_t)length;
    memcpy(at + STRING_LENGTH_BYTES, text, length);
    return at + STRING_LENGTH_BYTES + length;
}

bool crux_ssh_wire_host_key_blob(crux_ssh_key_type_t type, const uint8_t *host_key,
                                 size_t host_key_length, uint8_t *blob, size_t capacity,
                                 size_t *blob_length)
{
    const key_form_t *form = form_of(type);
    size_t needed;
    uint8_t *at;

    if (blob_length != NULL) {
        *blob_length = 0u;
    }
    if (form == NULL || !crux_ssh_wire_host_key_form(type, host_key, host_key_length)) {
        return false;
    }
    const uint8_t *public_part = host_key + CRUX_SSH_WIRE_HOST_KEY_PRIVATE_BYTES;
    needed = STRING_LENGTH_BYTES + strlen(form->name) + STRING_LENGTH_BYTES + form->point_length;
    if (form->curve != NULL) {
        needed += STRING_LENGTH_BYTES + strlen(form->curve);
    }
    if (blob == NULL || blob_length == NULL || capacity < needed) {
        return false;
    }
    at = put_string(blob, form->name, strlen(form->name));
    if (form->curve != NULL) {
        at = put_string(at, form->curve, strlen(form->curve));
    }
    at = put_string(at, public_part, form->point_length);
    *blob_length = (size_t)(at - blob);
    return true;
}

bool crux_ssh_wire_fingerprint_text(const uint8_t digest[CRUX_SSH_WIRE_DIGEST_BYTES], char *text,
                                    size_t size)
{
    static const char label[] = "SHA256:";
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t bits = 0u;
    unsigned bit_count = 0u;
    char *out;

    if (text != NULL && size > 0u) {
        text[0] = '\0';
    }
    if (digest == NULL || text == NULL || size < CRUX_SSH_WIRE_FINGERPRINT_SIZE) {
        return false;
    }
    memcpy(text, label, sizeof(label) - 1u);
    out = text + sizeof(label) - 1u;
    for (size_t i = 0; i < CRUX_SSH_WIRE_DIGEST_BYTES; ++i) {
        bits = (bits << 8u) | digest[i];
        bit_count += 8u;
        while (bit_count >= 6u) {
            bit_count -= 6u;
            *out++ = alphabet[(bits >> bit_count) & 0x3fu];
        }
    }
    if (bit_count > 0u) {
        *out++ = alphabet[(bits << (6u - bit_count)) & 0x3fu];
    }
    *out = '\0';
    return true;
}
