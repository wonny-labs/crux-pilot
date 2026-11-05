#include "cli_internal.h"

#include <limits.h>
#include <string.h>

/* An escape sequence longer than this is not one a terminal sends: its later bytes are input. */
#define CLI_ESCAPE_MAX 32u

#define BYTE_INTERRUPT 0x03u
#define BYTE_BACKSPACE 0x08u
#define BYTE_BELL 0x07u
#define BYTE_KILL 0x15u
#define BYTE_ESCAPE 0x1Bu
#define BYTE_DELETE 0x7Fu

typedef enum {
    ESCAPE_NONE,
    ESCAPE_START,        /* After ESC. */
    ESCAPE_CSI,          /* ESC [ with parameters until a final byte. */
    ESCAPE_ONE_MORE,     /* ESC O, which takes one more byte. */
    ESCAPE_INTERMEDIATE, /* ESC with intermediates until a final byte. */
    ESCAPE_STRING,       /* ESC ] and friends, until BEL or ESC \. */
    ESCAPE_STRING_END    /* An ESC inside a string. */
} escape_state_t;

void crux_cli_secure_zero(void *memory, size_t length)
{
    volatile unsigned char *bytes = memory;
    for (size_t index = 0; index < length; ++index) {
        bytes[index] = 0;
    }
}

void crux_cli_input_reset(crux_cli_context_t *context)
{
    context->reserved.escape_state = ESCAPE_NONE;
    context->reserved.escape_length = 0;
    context->reserved.last_was_cr = false;
}

static bool in_range(uint8_t byte, uint8_t low, uint8_t high)
{
    return byte >= low && byte <= high;
}

static crux_cli_input_action_t classify_plain(crux_cli_context_t *context, uint8_t byte)
{
    if (byte == '\n' && context->reserved.last_was_cr) {
        context->reserved.last_was_cr = false;
        return CRUX_CLI_INPUT_NONE;
    }
    context->reserved.last_was_cr = byte == '\r';
    switch (byte) {
    case '\r':
    case '\n':
        return CRUX_CLI_INPUT_LINE_END;
    case BYTE_BACKSPACE:
    case BYTE_DELETE:
        return CRUX_CLI_INPUT_ERASE;
    case BYTE_KILL:
        return CRUX_CLI_INPUT_KILL;
    case BYTE_INTERRUPT:
        return CRUX_CLI_INPUT_INTERRUPT;
    case BYTE_ESCAPE:
        context->reserved.escape_state = ESCAPE_START;
        context->reserved.escape_length = 0;
        return CRUX_CLI_INPUT_NONE;
    default:
        return byte < 0x20u ? CRUX_CLI_INPUT_NONE : CRUX_CLI_INPUT_BYTE;
    }
}

/* Moves the sequence on by one byte. False means the byte does not belong to a sequence: the
 * sequence is over and the byte is ordinary input, so a line end or an interrupt still acts. */
static bool escape_consume(crux_cli_context_t *context, uint8_t byte)
{
    uint8_t *state = &context->reserved.escape_state;
    switch ((escape_state_t)*state) {
    case ESCAPE_NONE:
        return false;
    case ESCAPE_START:
        if (byte == BYTE_ESCAPE) {
            return true;
        }
        if (byte == '[') {
            *state = ESCAPE_CSI;
        } else if (byte == 'O') {
            *state = ESCAPE_ONE_MORE;
        } else if (byte == ']' || byte == 'P' || byte == 'X' || byte == '^' || byte == '_') {
            *state = ESCAPE_STRING;
        } else if (in_range(byte, 0x20u, 0x2Fu)) {
            *state = ESCAPE_INTERMEDIATE;
        } else if (in_range(byte, 0x30u, 0x7Eu)) {
            *state = ESCAPE_NONE;
        } else {
            return false;
        }
        return true;
    case ESCAPE_CSI:
        if (byte == BYTE_ESCAPE) {
            *state = ESCAPE_START;
        } else if (in_range(byte, 0x40u, 0x7Eu)) {
            *state = ESCAPE_NONE;
        } else if (!in_range(byte, 0x20u, 0x3Fu)) {
            return false;
        }
        return true;
    case ESCAPE_ONE_MORE:
        if (byte == BYTE_ESCAPE) {
            *state = ESCAPE_START;
        } else if (in_range(byte, 0x20u, 0x7Eu)) {
            *state = ESCAPE_NONE;
        } else {
            return false;
        }
        return true;
    case ESCAPE_INTERMEDIATE:
        if (byte == BYTE_ESCAPE) {
            *state = ESCAPE_START;
        } else if (in_range(byte, 0x30u, 0x7Eu)) {
            *state = ESCAPE_NONE;
        } else if (!in_range(byte, 0x20u, 0x2Fu)) {
            return false;
        }
        return true;
    case ESCAPE_STRING:
        if (byte == BYTE_BELL) {
            *state = ESCAPE_NONE;
        } else if (byte == BYTE_ESCAPE) {
            *state = ESCAPE_STRING_END;
        } else if (byte < 0x20u) {
            return false;
        }
        return true;
    case ESCAPE_STRING_END:
        if (byte == '\\') {
            *state = ESCAPE_NONE;
            return true;
        }
        return false;
    }
    return false;
}

crux_cli_input_action_t crux_cli_input_feed(crux_cli_context_t *context, uint8_t byte)
{
    if (context->reserved.escape_state != ESCAPE_NONE) {
        context->reserved.escape_length += 1;
        if (context->reserved.escape_length <= CLI_ESCAPE_MAX && escape_consume(context, byte)) {
            /* A sequence is what came after the line end, if anything did. */
            context->reserved.last_was_cr = false;
            return CRUX_CLI_INPUT_NONE;
        }
        context->reserved.escape_state = ESCAPE_NONE;
    }
    return classify_plain(context, byte);
}

crux_cli_result_t crux_cli_send(const crux_cli_io_t *io, crux_access_session_t session,
                                const void *data, size_t length)
{
    const unsigned char *bytes = data;
    size_t remaining = length;
    unsigned attempts = 0;
    while (remaining > 0) {
        size_t chunk = remaining > (size_t)INT_MAX ? (size_t)INT_MAX : remaining;
        int accepted = io->write(io->transport, bytes, chunk, CRUX_CLI_WRITE_TIMEOUT_MS);
        if (accepted < 0 || (size_t)accepted > chunk) {
            return CRUX_CLI_ERR_OUTPUT;
        }
        if (accepted > 0) {
            bytes += accepted;
            remaining -= (size_t)accepted;
            attempts = 0;
            continue;
        }
        if (session.value != 0) {
            char name[CRUX_ACCESS_NAME_SIZE];
            if (crux_access_session_user(session, name, sizeof(name)) != CRUX_ACCESS_OK) {
                return CRUX_CLI_ERR_OUTPUT;
            }
        } else {
            attempts += 1;
            if (attempts >= CRUX_CLI_WRITE_ATTEMPTS_NO_SESSION) {
                return CRUX_CLI_ERR_OUTPUT;
            }
        }
    }
    return CRUX_CLI_OK;
}

crux_cli_result_t crux_cli_send_text(const crux_cli_io_t *io, crux_access_session_t session,
                                     const char *text)
{
    return crux_cli_send(io, session, text, strlen(text));
}

crux_cli_result_t crux_cli_send_password_rejected(const crux_cli_io_t *io,
                                                  crux_access_session_t session)
{
    const char *rule = crux_cli_registry_password_rule();
    crux_cli_result_t result = crux_cli_send_text(io, session, CRUX_CLI_TEXT_REJECTED);
    if (result == CRUX_CLI_OK && rule != NULL) {
        result = crux_cli_send_text(io, session, ": ");
        if (result == CRUX_CLI_OK) {
            result = crux_cli_send_text(io, session, rule);
        }
    }
    return result == CRUX_CLI_OK ? crux_cli_send_text(io, session, "\n") : result;
}

size_t crux_cli_format_decimal(char *text, uint32_t value)
{
    char reversed[10];
    size_t count = 0;
    do {
        reversed[count] = (char)('0' + (value % 10u));
        value /= 10u;
        count += 1;
    } while (value != 0);
    for (size_t index = 0; index < count; ++index) {
        text[index] = reversed[count - 1u - index];
    }
    text[count] = '\0';
    return count;
}

crux_cli_result_t crux_cli_send_retry(const crux_cli_io_t *io, crux_access_session_t session,
                                      uint32_t retry_after_ms)
{
    char number[11];
    uint32_t seconds = retry_after_ms / 1000u + (retry_after_ms % 1000u != 0 ? 1u : 0u);
    (void)crux_cli_format_decimal(number, seconds);
    crux_cli_result_t result = crux_cli_send_text(io, session, "try again in ");
    if (result == CRUX_CLI_OK) {
        result = crux_cli_send_text(io, session, number);
    }
    return result == CRUX_CLI_OK ? crux_cli_send_text(io, session, " s\n") : result;
}

static bool session_is_over(crux_access_session_t session)
{
    char name[CRUX_ACCESS_NAME_SIZE];
    return session.value != 0 &&
           crux_access_session_user(session, name, sizeof(name)) != CRUX_ACCESS_OK;
}

/* One edit of the input so far. Echo goes through the same send as all output. */
static bool erase_echo(const crux_cli_io_t *io, crux_access_session_t session, size_t count)
{
    for (size_t index = 0; index < count; ++index) {
        if (crux_cli_send_text(io, session, "\b \b") != CRUX_CLI_OK) {
            return false;
        }
    }
    return true;
}

/* The one reader. A line and a secret differ in where the bytes go and whether they are echoed.
 * Bytes arrive one at a time, so nothing typed sits in a pending-input buffer here. */
static crux_cli_read_t read_input(crux_cli_context_t *context, const crux_cli_io_t *io,
                                  crux_access_session_t session, char *buffer, size_t limit,
                                  bool echo_bytes, unsigned idle_polls, size_t *length)
{
    size_t used = 0;
    bool overflow = false;
    unsigned idle = 0;
    unsigned unchecked = 0;
    uint8_t byte = 0;
    crux_cli_read_t outcome = CRUX_CLI_READ_TRANSPORT;

    for (;;) {
        int got = io->read(io->transport, &byte, 1u, CRUX_CLI_READ_TIMEOUT_MS);
        if (got < 0 || got > 1) {
            break;
        }
        if (got == 0) {
            if (session_is_over(session)) {
                outcome = CRUX_CLI_READ_SESSION;
                break;
            }
            idle += 1;
            if (idle_polls != 0 && idle >= idle_polls) {
                outcome = !io->echo || crux_cli_send_text(io, session, "\n") == CRUX_CLI_OK
                              ? CRUX_CLI_READ_IDLE
                              : CRUX_CLI_READ_TRANSPORT;
                break;
            }
            continue;
        }
        idle = 0;
        /* A stream with no line end and no timeout must not outlive the session's limits. */
        unchecked += 1;
        if (unchecked >= CRUX_CLI_SESSION_CHECK_BYTES) {
            unchecked = 0;
            if (session_is_over(session)) {
                outcome = CRUX_CLI_READ_SESSION;
                break;
            }
        }
        crux_cli_input_action_t action = crux_cli_input_feed(context, byte);
        if (action == CRUX_CLI_INPUT_LINE_END) {
            if (io->echo && crux_cli_send_text(io, session, "\n") != CRUX_CLI_OK) {
                break;
            }
            outcome = overflow ? CRUX_CLI_READ_TOO_LONG : CRUX_CLI_READ_OK;
            break;
        }
        if (action == CRUX_CLI_INPUT_INTERRUPT) {
            outcome = !io->echo || crux_cli_send_text(io, session, "^C\n") == CRUX_CLI_OK
                          ? CRUX_CLI_READ_INTERRUPTED
                          : CRUX_CLI_READ_TRANSPORT;
            break;
        }
        if (action == CRUX_CLI_INPUT_ERASE) {
            if (!overflow && used > 0) {
                used -= 1;
                if (echo_bytes && io->echo && !erase_echo(io, session, 1)) {
                    break;
                }
            }
        } else if (action == CRUX_CLI_INPUT_KILL) {
            if (echo_bytes && io->echo && !erase_echo(io, session, used)) {
                break;
            }
            used = 0;
            overflow = false;
        } else if (action == CRUX_CLI_INPUT_BYTE) {
            if (used >= limit) {
                overflow = true;
            } else if (!overflow) {
                buffer[used] = (char)byte;
                used += 1;
                if (echo_bytes && io->echo &&
                    crux_cli_send(io, session, &byte, 1u) != CRUX_CLI_OK) {
                    break;
                }
            }
        }
    }
    crux_cli_secure_zero(&byte, sizeof(byte));
    buffer[used] = '\0';
    *length = used;
    return outcome;
}

crux_cli_read_t crux_cli_read_line(crux_cli_context_t *context, const crux_cli_io_t *io,
                                   crux_access_session_t session, size_t *length)
{
    return read_input(context, io, session, context->line, CRUX_CLI_LINE_MAX, true, 0, length);
}

crux_cli_read_t crux_cli_read_hidden(crux_cli_context_t *context, const crux_cli_io_t *io,
                                     crux_access_session_t session, char *buffer, size_t capacity,
                                     size_t *length)
{
    /* A session has its own idle limit. With none, an abandoned prompt must not hold a secret. */
    unsigned idle_polls = session.value == 0 ? CRUX_CLI_SECRET_IDLE_POLLS : 0u;
    crux_cli_read_t outcome =
        read_input(context, io, session, buffer, capacity - 1u, false, idle_polls, length);
    if (outcome != CRUX_CLI_READ_OK) {
        crux_cli_secure_zero(buffer, capacity);
        *length = 0;
    }
    return outcome;
}

int crux_cli_write_terminal(
    crux_cli_line_ends_t *state,
    int (*write)(void *transport, const void *buffer, size_t length, uint32_t timeout_ms),
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): as crux_cli_io_t.
    void *transport, const void *buffer, size_t length, uint32_t timeout_ms)
{
    const char *bytes = buffer;
    if (length == 0) {
        return 0;
    }
    if (bytes[0] != '\n') {
        const char *line_end = memchr(bytes, '\n', length);
        return write(transport, bytes, line_end != NULL ? (size_t)(line_end - bytes) : length,
                     timeout_ms);
    }
    if (!state->carriage_return_sent) {
        int sent = write(transport, "\r", 1u, timeout_ms);
        if (sent <= 0) {
            return sent;
        }
        state->carriage_return_sent = true;
    }
    int sent = write(transport, "\n", 1u, timeout_ms);
    if (sent <= 0) {
        return sent;
    }
    state->carriage_return_sent = false;
    return 1;
}
