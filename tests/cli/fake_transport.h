/* A scripted transport for the CLI's Host tests. Single threaded and deterministic: it never waits,
 * so a timeout returns at once. This is a test composition, and no build option can select it. */
#ifndef CRUX_CLI_FAKE_TRANSPORT_H
#define CRUX_CLI_FAKE_TRANSPORT_H

#include <crux_cli_adapter.h>

#define CLI_FAKE_INPUT_MAX 16384u
#define CLI_FAKE_OUTPUT_MAX 65536u
#define CLI_FAKE_TIMEOUTS_MAX 32u

typedef void (*cli_fake_hook_t)(void *context);

typedef struct {
    uint8_t input[CLI_FAKE_INPUT_MAX];
    size_t input_length;
    size_t input_position;
    /* After input is consumed up to position, read reports this many timeouts. */
    struct {
        size_t position;
        unsigned count;
    } timeouts[CLI_FAKE_TIMEOUTS_MAX];
    size_t timeout_count;
    /* When input runs out: false ends the transport, true keeps timing out. */
    bool wait_when_empty;
    /* When waiting with no input left, the transport ends after this many timeouts. */
    unsigned empty_timeouts_left;

    char output[CLI_FAKE_OUTPUT_MAX];
    size_t output_length;

    size_t write_limit;      /* Most bytes one write accepts. Zero means no limit. */
    unsigned write_timeouts; /* The next writes time out, this many of them. */
    bool write_stuck;        /* Every write times out. */
    bool write_failed;       /* Every write fails. */
    size_t write_fail_after; /* Fails once this many bytes are accepted. SIZE_MAX means never. */
    bool write_overclaims;   /* Reports more bytes than it was asked to take. */
    bool read_overclaims;    /* Reports more bytes than it was asked to give. */

    /* Run on a timeout, from inside the read or write. */
    cli_fake_hook_t on_read_timeout;
    cli_fake_hook_t on_write_timeout;
    /* Run once when input_position first reaches hook_position. */
    cli_fake_hook_t on_position;
    size_t hook_position;
    void *hook_context;

    unsigned read_calls;
    unsigned read_timeouts_seen;
    unsigned write_calls;
    unsigned write_timeouts_seen;
} cli_fake_t;

void cli_fake_reset(cli_fake_t *fake);
/* Appends input bytes. A text argument appends without its terminator. */
void cli_fake_input(cli_fake_t *fake, const char *text);
void cli_fake_input_bytes(cli_fake_t *fake, const void *data, size_t length);
/* Read reports this many timeouts before the next appended byte. */
void cli_fake_input_timeouts(cli_fake_t *fake, unsigned count);
/* Forgets what was written. */
void cli_fake_output_clear(cli_fake_t *fake);
/* The transport, for a crux_cli_io_t. The fake must outlive it. */
crux_cli_io_t cli_fake_io(cli_fake_t *fake, bool echo);
int cli_fake_read(void *transport, void *buffer, size_t length, uint32_t timeout_ms);
int cli_fake_write(void *transport, const void *buffer, size_t length, uint32_t timeout_ms);

#endif
