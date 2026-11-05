/* A bounded reader for the two request bodies of the session routes: one JSON object whose members
 * are named strings. RFC 8259 escapes are decoded, including surrogate pairs, and a lone surrogate,
 * a raw control character, an unknown, repeated or missing member, or any other value is malformed.
 * It allocates nothing and does not recurse. */
#include "web_session_internal.h"

#include <string.h>

#define KEY_MAX 32u

typedef struct {
    const uint8_t *cursor;
    const uint8_t *end;
} reader_t;

static void skip_space(reader_t *reader)
{
    while (reader->cursor < reader->end && (*reader->cursor == ' ' || *reader->cursor == '\t' ||
                                            *reader->cursor == '\n' || *reader->cursor == '\r')) {
        ++reader->cursor;
    }
}

static bool take(reader_t *reader, uint8_t expected)
{
    skip_space(reader);
    if (reader->cursor < reader->end && *reader->cursor == expected) {
        ++reader->cursor;
        return true;
    }
    return false;
}

static bool hex4(reader_t *reader, uint32_t *value)
{
    *value = 0u;
    if (reader->end - reader->cursor < 4) {
        return false;
    }
    for (int index = 0; index < 4; ++index) {
        uint8_t character = *reader->cursor++;
        uint32_t digit;

        if (character >= '0' && character <= '9') {
            digit = (uint32_t)(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            digit = (uint32_t)(character - 'a' + 10);
        } else if (character >= 'A' && character <= 'F') {
            digit = (uint32_t)(character - 'A' + 10);
        } else {
            return false;
        }
        *value = (*value << 4) | digit;
    }
    return true;
}

/* Appends bytes to the output, counting what does not fit. */
typedef struct {
    char *buffer;
    size_t size;
    size_t length;
    bool holds_nul;
} output_t;

static void put(output_t *output, uint8_t byte)
{
    if (output->buffer != NULL && output->length + 1u < output->size) {
        output->buffer[output->length] = (char)byte;
    }
    ++output->length;
}

static void put_code_point(output_t *output, uint32_t code)
{
    if (code == 0u) {
        output->holds_nul = true;
    }
    if (code < 0x80u) {
        put(output, (uint8_t)code);
    } else if (code < 0x800u) {
        put(output, (uint8_t)(0xc0u | (code >> 6)));
        put(output, (uint8_t)(0x80u | (code & 0x3fu)));
    } else if (code < 0x10000u) {
        put(output, (uint8_t)(0xe0u | (code >> 12)));
        put(output, (uint8_t)(0x80u | ((code >> 6) & 0x3fu)));
        put(output, (uint8_t)(0x80u | (code & 0x3fu)));
    } else {
        put(output, (uint8_t)(0xf0u | (code >> 18)));
        put(output, (uint8_t)(0x80u | ((code >> 12) & 0x3fu)));
        put(output, (uint8_t)(0x80u | ((code >> 6) & 0x3fu)));
        put(output, (uint8_t)(0x80u | (code & 0x3fu)));
    }
}

static bool escape(reader_t *reader, output_t *output)
{
    uint32_t code;
    uint32_t low;
    uint8_t character;

    if (reader->cursor >= reader->end) {
        return false;
    }
    character = *reader->cursor++;
    switch (character) {
    case '"':
    case '\\':
    case '/':
        put(output, character);
        return true;
    case 'b':
        put(output, '\b');
        return true;
    case 'f':
        put(output, '\f');
        return true;
    case 'n':
        put(output, '\n');
        return true;
    case 'r':
        put(output, '\r');
        return true;
    case 't':
        put(output, '\t');
        return true;
    case 'u':
        break;
    default:
        return false;
    }
    if (!hex4(reader, &code)) {
        return false;
    }
    if (code >= 0xdc00u && code <= 0xdfffu) {
        return false;
    }
    if (code >= 0xd800u && code <= 0xdbffu) {
        if (reader->end - reader->cursor < 2 || reader->cursor[0] != '\\' ||
            reader->cursor[1] != 'u') {
            return false;
        }
        reader->cursor += 2;
        if (!hex4(reader, &low) || low < 0xdc00u || low > 0xdfffu) {
            return false;
        }
        code = 0x10000u + ((code - 0xd800u) << 10) + (low - 0xdc00u);
    }
    put_code_point(output, code);
    return true;
}

/* A string, after its opening quote has been taken. Bytes of 0x80 and above pass as they are. */
static bool string_value(reader_t *reader, output_t *output)
{
    while (reader->cursor < reader->end) {
        uint8_t character = *reader->cursor++;

        if (character == '"') {
            if (output->buffer != NULL && output->size != 0u) {
                output->buffer[output->length < output->size ? output->length : output->size - 1u] =
                    '\0';
            }
            return true;
        }
        if (character < 0x20u) {
            return false;
        }
        if (character == '\\') {
            if (!escape(reader, output)) {
                return false;
            }
        } else {
            put(output, character);
        }
    }
    return false;
}

static web_json_field_t *field_named(web_json_field_t *fields, size_t count, const char *key,
                                     size_t length)
{
    for (size_t index = 0; index < count; ++index) {
        if (strlen(fields[index].name) == length && memcmp(fields[index].name, key, length) == 0) {
            return &fields[index];
        }
    }
    return NULL;
}

static bool member(reader_t *reader, web_json_field_t *fields, size_t count)
{
    char key[KEY_MAX];
    output_t key_output = {key, sizeof(key), 0u, false};
    output_t value_output;
    web_json_field_t *field;

    if (!take(reader, '"') || !string_value(reader, &key_output) || key_output.length >= KEY_MAX ||
        key_output.holds_nul) {
        return false;
    }
    field = field_named(fields, count, key, key_output.length);
    if (field == NULL || field->seen || !take(reader, ':') || !take(reader, '"')) {
        return false;
    }
    value_output = (output_t){field->buffer, field->size, 0u, false};
    if (!string_value(reader, &value_output)) {
        return false;
    }
    field->seen = true;
    field->length = value_output.length;
    field->holds_nul = value_output.holds_nul;
    return true;
}

bool web_json_parse_strings(const uint8_t *body, size_t length, web_json_field_t *fields,
                            size_t count)
{
    reader_t reader = {body, body + length};

    for (size_t index = 0; index < count; ++index) {
        fields[index].seen = false;
        fields[index].length = 0u;
        fields[index].holds_nul = false;
        if (fields[index].buffer != NULL && fields[index].size != 0u) {
            fields[index].buffer[0] = '\0';
        }
    }
    if (body == NULL || !take(&reader, '{')) {
        return false;
    }
    if (!take(&reader, '}')) {
        do {
            if (!member(&reader, fields, count)) {
                return false;
            }
        } while (take(&reader, ','));
        if (!take(&reader, '}')) {
            return false;
        }
    }
    skip_space(&reader);
    if (reader.cursor != reader.end) {
        return false;
    }
    for (size_t index = 0; index < count; ++index) {
        if (!fields[index].seen) {
            return false;
        }
    }
    return true;
}
