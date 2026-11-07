/* The bounded JSON of the management routes: a writer over a fixed buffer, and a reader for the one
 * request body that they take, `{"value": V}`. Neither allocates, and the reader does not recurse.
 * The escapes it decodes are those of RFC 8259, including surrogate pairs. */
#include "web_management_internal.h"

#include <stdio.h>
#include <string.h>

void web_mgmt_writer_init(web_mgmt_writer_t *writer, char *data, size_t size)
{
    writer->data = data;
    writer->size = size;
    writer->used = 0u;
    writer->overflow = size == 0u;
    if (size != 0u) {
        data[0] = '\0';
    }
}

static void put(web_mgmt_writer_t *writer, const char *bytes, size_t length)
{
    if (writer->overflow) {
        return;
    }
    /* One byte stays for the terminator, so that the buffer is always a string. */
    if (length >= writer->size - writer->used) {
        writer->overflow = true;
        return;
    }
    memcpy(writer->data + writer->used, bytes, length);
    writer->used += length;
    writer->data[writer->used] = '\0';
}

void web_mgmt_write(web_mgmt_writer_t *writer, const char *text)
{
    put(writer, text, strlen(text));
}

void web_mgmt_write_u32(web_mgmt_writer_t *writer, uint32_t value)
{
    char digits[11];
    int length = snprintf(digits, sizeof(digits), "%lu", (unsigned long)value);

    put(writer, digits, length > 0 ? (size_t)length : 0u);
}

void web_mgmt_write_string(web_mgmt_writer_t *writer, const char *text, size_t length)
{
    static const char hex[] = "0123456789abcdef";

    put(writer, "\"", 1u);
    for (size_t index = 0; index < length; ++index) {
        unsigned char character = (unsigned char)text[index];

        if (character == '"' || character == '\\') {
            const char pair[2] = {'\\', (char)character};

            put(writer, pair, sizeof(pair));
        } else if (character < 0x20u) {
            const char escape[6] = {
                '\\', 'u', '0', '0', hex[character >> 4], hex[character & 0xfu]};

            put(writer, escape, sizeof(escape));
        } else {
            put(writer, &text[index], 1u);
        }
    }
    put(writer, "\"", 1u);
}

/* The reader. */

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

static bool take_word(reader_t *reader, const char *word)
{
    size_t length = strlen(word);

    if ((size_t)(reader->end - reader->cursor) < length ||
        memcmp(reader->cursor, word, length) != 0) {
        return false;
    }
    reader->cursor += length;
    return true;
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

/* Decoded bytes go to the buffer while they fit, and every one is counted. */
typedef struct {
    char *buffer;
    size_t size;
    size_t length;
} output_t;

/* The buffer holds a string after every byte, so that a body refused part way through a string
 * leaves no unterminated text for a caller that reads it anyway. */
static void out(output_t *output, uint8_t byte)
{
    if (output->buffer != NULL && output->length + 1u < output->size) {
        output->buffer[output->length] = (char)byte;
        output->buffer[output->length + 1u] = '\0';
    }
    ++output->length;
}

static void out_code_point(output_t *output, uint32_t code)
{
    if (code < 0x80u) {
        out(output, (uint8_t)code);
    } else if (code < 0x800u) {
        out(output, (uint8_t)(0xc0u | (code >> 6)));
        out(output, (uint8_t)(0x80u | (code & 0x3fu)));
    } else if (code < 0x10000u) {
        out(output, (uint8_t)(0xe0u | (code >> 12)));
        out(output, (uint8_t)(0x80u | ((code >> 6) & 0x3fu)));
        out(output, (uint8_t)(0x80u | (code & 0x3fu)));
    } else {
        out(output, (uint8_t)(0xf0u | (code >> 18)));
        out(output, (uint8_t)(0x80u | ((code >> 12) & 0x3fu)));
        out(output, (uint8_t)(0x80u | ((code >> 6) & 0x3fu)));
        out(output, (uint8_t)(0x80u | (code & 0x3fu)));
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
        out(output, character);
        return true;
    case 'b':
        out(output, '\b');
        return true;
    case 'f':
        out(output, '\f');
        return true;
    case 'n':
        out(output, '\n');
        return true;
    case 'r':
        out(output, '\r');
        return true;
    case 't':
        out(output, '\t');
        return true;
    case 'u':
        break;
    default:
        return false;
    }
    if (!hex4(reader, &code) || (code >= 0xdc00u && code <= 0xdfffu)) {
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
    out_code_point(output, code);
    return true;
}

/* A string after its opening quote. Bytes of 0x80 and above pass as they are: the configuration
 * service holds the rule for what a string may contain. */
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
            out(output, character);
        }
    }
    return false;
}

static bool digit(const reader_t *reader)
{
    return reader->cursor < reader->end && *reader->cursor >= '0' && *reader->cursor <= '9';
}

/* The grammar of a JSON number, with no bound on its digits. It is a u32 only as plain digits. */
static bool number_value(reader_t *reader, web_value_t *value)
{
    uint64_t accumulated = 0u;
    bool plain = true;
    bool overflow = false;

    if (reader->cursor < reader->end && *reader->cursor == '-') {
        plain = false;
        ++reader->cursor;
    }
    if (!digit(reader)) {
        return false;
    }
    if (*reader->cursor == '0') {
        ++reader->cursor;
        if (digit(reader)) {
            return false; /* A leading zero. */
        }
    } else {
        while (digit(reader)) {
            if (!overflow) {
                accumulated = accumulated * 10u + (uint64_t)(*reader->cursor - '0');
                overflow = accumulated > UINT32_MAX;
            }
            ++reader->cursor;
        }
    }
    if (reader->cursor < reader->end && *reader->cursor == '.') {
        plain = false;
        ++reader->cursor;
        if (!digit(reader)) {
            return false;
        }
        while (digit(reader)) {
            ++reader->cursor;
        }
    }
    if (reader->cursor < reader->end && (*reader->cursor == 'e' || *reader->cursor == 'E')) {
        plain = false;
        ++reader->cursor;
        if (reader->cursor < reader->end && (*reader->cursor == '+' || *reader->cursor == '-')) {
            ++reader->cursor;
        }
        if (!digit(reader)) {
            return false;
        }
        while (digit(reader)) {
            ++reader->cursor;
        }
    }
    value->kind = WEB_VALUE_NUMBER;
    value->number_ok = plain && !overflow;
    value->number = value->number_ok ? (uint32_t)accumulated : 0u;
    return true;
}

static bool member_value(reader_t *reader, web_value_t *value)
{
    skip_space(reader);
    if (reader->cursor >= reader->end) {
        return false;
    }
    if (*reader->cursor == '"') {
        output_t output = {value->text, value->size, 0u};

        ++reader->cursor;
        value->kind = WEB_VALUE_STRING;
        if (!string_value(reader, &output)) {
            return false;
        }
        value->length = output.length;
        return true;
    }
    if (take_word(reader, "true")) {
        value->kind = WEB_VALUE_BOOL;
        value->boolean = true;
        return true;
    }
    if (take_word(reader, "false")) {
        value->kind = WEB_VALUE_BOOL;
        value->boolean = false;
        return true;
    }
    return number_value(reader, value);
}

bool web_mgmt_parse_update(const uint8_t *body, size_t length, web_value_t *value)
{
    static const char name[] = "value";
    reader_t reader = {body, body + length};
    char key[sizeof(name) + 1u];
    output_t key_output = {key, sizeof(key), 0u};
    char *text = value->text;
    size_t size = value->size;

    memset(value, 0, sizeof(*value));
    value->text = text;
    value->size = size;
    if (text != NULL && size != 0u) {
        text[0] = '\0';
    }
    if (body == NULL || !take(&reader, '{') || !take(&reader, '"') ||
        !string_value(&reader, &key_output) || key_output.length != sizeof(name) - 1u ||
        memcmp(key, name, sizeof(name) - 1u) != 0 || !take(&reader, ':') ||
        !member_value(&reader, value) || !take(&reader, '}')) {
        return false;
    }
    skip_space(&reader);
    return reader.cursor == reader.end;
}
