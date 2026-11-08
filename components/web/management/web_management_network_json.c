/* The reader of the body of PUT /api/v1/network. A bounded recursive-descent over one object of
 * three members, with no allocation and no recursion. A strict reader is the point: the body is a
 * client's, and a form that two parsers read differently is where a check is bypassed. */
#include "web_management_network.h"

#include <string.h>

#define TEXT_MAX 15u /* A dotted quad is at most 15 characters. */

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t position;
} reader_t;

static bool white(uint8_t byte)
{
    return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r';
}

static void skip_white(reader_t *reader)
{
    while (reader->position < reader->length && white(reader->data[reader->position])) {
        ++reader->position;
    }
}

static bool expect(reader_t *reader, uint8_t byte)
{
    skip_white(reader);
    if (reader->position < reader->length && reader->data[reader->position] == byte) {
        ++reader->position;
        return true;
    }
    return false;
}

static int hex_digit(uint8_t byte)
{
    if (byte >= '0' && byte <= '9') {
        return byte - '0';
    }
    if (byte >= 'a' && byte <= 'f') {
        return byte - 'a' + 10;
    }
    if (byte >= 'A' && byte <= 'F') {
        return byte - 'A' + 10;
    }
    return -1;
}

/* Reads a string whose decoded form is at most TEXT_MAX bytes of ASCII, which is all that a key of
 * this object or an address can be. A longer or wider string is not either, so it is refused. */
static bool read_string(reader_t *reader, char text[TEXT_MAX + 1u], size_t *length)
{
    size_t used = 0u;

    skip_white(reader);
    if (reader->position >= reader->length || reader->data[reader->position] != '"') {
        return false;
    }
    ++reader->position;
    while (reader->position < reader->length) {
        uint8_t byte = reader->data[reader->position++];
        uint32_t code = byte;

        if (byte == '"') {
            text[used] = '\0';
            *length = used;
            return true;
        }
        if (byte < 0x20u || byte >= 0x80u) {
            return false;
        }
        if (byte == '\\') {
            if (reader->position >= reader->length) {
                return false;
            }
            byte = reader->data[reader->position++];
            switch (byte) {
            case '"':
            case '\\':
            case '/':
                code = byte;
                break;
            case 'b':
                code = '\b';
                break;
            case 'f':
                code = '\f';
                break;
            case 'n':
                code = '\n';
                break;
            case 'r':
                code = '\r';
                break;
            case 't':
                code = '\t';
                break;
            case 'u':
                code = 0u;
                if (reader->length - reader->position < 4u) {
                    return false;
                }
                for (unsigned digit = 0; digit < 4u; ++digit) {
                    int value = hex_digit(reader->data[reader->position++]);

                    if (value < 0) {
                        return false;
                    }
                    code = (code << 4) | (uint32_t)value;
                }
                /* Only ASCII can be part of what this object holds, so a wider escape, a
                 * surrogate included, is refused here. */
                if (code >= 0x80u) {
                    return false;
                }
                break;
            default:
                return false;
            }
        }
        /* A NUL would end the key early for strcmp, so that "address\u0000x" would read as address.
         */
        if (code == 0u || used >= TEXT_MAX) {
            return false;
        }
        text[used++] = (char)code;
    }
    return false;
}

/* A JSON integer. Returns false for anything else, a fraction and an exponent included. */
static bool read_integer(reader_t *reader, uint32_t *value)
{
    bool negative = false;
    uint64_t number = 0u;
    size_t digits = 0u;

    skip_white(reader);
    if (reader->position < reader->length && reader->data[reader->position] == '-') {
        negative = true;
        ++reader->position;
    }
    while (reader->position < reader->length && reader->data[reader->position] >= '0' &&
           reader->data[reader->position] <= '9') {
        if (digits == 1u && number == 0u) {
            return false; /* A leading zero. */
        }
        number = number * 10u + (uint64_t)(reader->data[reader->position] - '0');
        if (number > UINT32_MAX) {
            number = UINT32_MAX;
        }
        ++reader->position;
        ++digits;
    }
    if (digits == 0u) {
        return false;
    }
    if (reader->position < reader->length) {
        uint8_t next = reader->data[reader->position];

        if (next == '.' || next == 'e' || next == 'E' || next == '+' || next == '-') {
            return false;
        }
    }
    *value = negative ? 0u : (uint32_t)number;
    return true;
}

bool web_mgmt_parse_network(const uint8_t *body, size_t length, crux_network_settings_t *settings)
{
    reader_t reader = {body, length, 0u};
    crux_network_settings_t found;
    bool seen_address = false;
    bool seen_prefix = false;
    bool seen_gateway = false;

    if (body == NULL || settings == NULL) {
        return false;
    }
    memset(&found, 0, sizeof(found));
    if (!expect(&reader, '{')) {
        return false;
    }
    for (;;) {
        char key[TEXT_MAX + 1u];
        char text[TEXT_MAX + 1u];
        size_t key_length = 0u;
        size_t text_length = 0u;
        uint32_t address;

        if (!read_string(&reader, key, &key_length) || !expect(&reader, ':')) {
            return false;
        }
        if (strcmp(key, "address") == 0 && !seen_address) {
            if (!read_string(&reader, text, &text_length) ||
                !crux_network_settings_parse_ipv4(text, text_length, &address)) {
                return false;
            }
            found.address = address;
            seen_address = true;
        } else if (strcmp(key, "gateway") == 0 && !seen_gateway) {
            if (!read_string(&reader, text, &text_length) ||
                !crux_network_settings_parse_ipv4(text, text_length, &address)) {
                return false;
            }
            found.has_gateway = true;
            found.gateway = address;
            seen_gateway = true;
        } else if (strcmp(key, "prefix_length") == 0 && !seen_prefix) {
            if (!read_integer(&reader, &found.prefix_length)) {
                return false;
            }
            seen_prefix = true;
        } else {
            /* Another member, or a repeated one. */
            return false;
        }
        skip_white(&reader);
        if (reader.position < reader.length && reader.data[reader.position] == ',') {
            ++reader.position;
            continue;
        }
        break;
    }
    if (!expect(&reader, '}')) {
        return false;
    }
    skip_white(&reader);
    if (reader.position != reader.length || !seen_address || !seen_prefix) {
        return false;
    }
    *settings = found;
    return true;
}
