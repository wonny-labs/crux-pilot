#include "cli_internal.h"

static bool is_control(unsigned char value)
{
    return value < 0x20u || value == 0x7Fu;
}

/* Reads one quoted value at *position and unescapes it in place, over its own opening quote. */
static bool take_quoted(char *line, size_t length, size_t *position)
{
    size_t in = *position + 1u;
    size_t out = *position;
    for (;;) {
        if (in >= length) {
            return false;
        }
        unsigned char value = (unsigned char)line[in];
        in += 1;
        if (value == '"') {
            break;
        }
        if (value == '\\') {
            if (in >= length) {
                return false;
            }
            value = (unsigned char)line[in];
            in += 1;
            if (value != '"' && value != '\\') {
                return false;
            }
        }
        if (is_control(value)) {
            return false;
        }
        line[out] = (char)value;
        out += 1;
    }
    if (in < length && line[in] != ' ') {
        return false;
    }
    /* out is behind in by at least the two quotes, so this overwrites nothing still unread. */
    line[out] = '\0';
    *position = in;
    return true;
}

/* Reads one bare word at *position, which holds only printable ASCII other than a quote. */
static bool take_bare(char *line, size_t length, size_t *position)
{
    size_t in = *position;
    while (in < length && line[in] != ' ') {
        unsigned char value = (unsigned char)line[in];
        if (value == '"' || value > 0x7Fu || is_control(value)) {
            return false;
        }
        in += 1;
    }
    if (in < length) {
        line[in] = '\0';
        in += 1;
    }
    *position = in;
    return true;
}

crux_cli_result_t crux_cli_tokenize(crux_cli_context_t *context, size_t length, size_t *count,
                                    bool *too_many)
{
    char *line = context->line;
    size_t position = 0;
    size_t tokens = 0;
    *count = 0;
    *too_many = false;
    line[length] = '\0';
    for (;;) {
        while (position < length && line[position] == ' ') {
            position += 1;
        }
        if (position >= length) {
            break;
        }
        if (tokens == CRUX_CLI_TOKENS_MAX) {
            *too_many = true;
            return CRUX_CLI_ERR_USAGE;
        }
        context->reserved.argv[tokens] = &line[position];
        bool taken = line[position] == '"' ? take_quoted(line, length, &position)
                                           : take_bare(line, length, &position);
        if (!taken) {
            return CRUX_CLI_ERR_USAGE;
        }
        tokens += 1;
    }
    *count = tokens;
    return CRUX_CLI_OK;
}
