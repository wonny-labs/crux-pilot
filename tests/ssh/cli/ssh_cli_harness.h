/*
 * Runs the real entry points of Issue #193 through the fake provider and its scripted client. The
 * fake runs each session on the calling thread, inside channel_request. A shim between the entry
 * points and the fake counts calls and injects what the fake cannot do on its own.
 *
 * This file uses no CLI test helper, so the scripted and the real access suites can both link it.
 */
#ifndef CRUX_SSH_CLI_HARNESS_H
#define CRUX_SSH_CLI_HARNESS_H

#include "crux_ssh_callbacks.h"
#include "crux_ssh_cli.h"
#include "fake_attachment.h"
#include "fake_provider.h"

typedef void (*ssh_cli_hook_t)(void *context);

typedef struct {
    size_t write_limit;         /* Most bytes one write accepts. Zero means no limit. */
    unsigned write_timeouts;    /* The next writes time out, this many of them. */
    unsigned read_timeouts;     /* The next reads time out, this many of them. */
    crux_status_t read_result;  /* Not CRUX_OK: every read answers this, with nothing read. */
    crux_status_t write_result; /* Not CRUX_OK: every write answers this, with nothing written. */
    bool read_says_nothing;     /* A read answers CRUX_OK with no byte, against the contract. */
    bool read_overclaims;       /* A read reports more than it was asked for. */
    bool write_says_nothing;    /* A write answers CRUX_OK with no byte, against the contract. */
    bool write_overclaims;      /* A write reports more than it was asked for. */
    bool line_end_overclaims;   /* The same for the CR of a line end only. */

    /* Run on each timeout, seen from the fake or injected, from inside the read or write. */
    ssh_cli_hook_t on_read_timeout;
    ssh_cli_hook_t on_write_timeout;
    void *hook_context;

    unsigned read_calls;
    unsigned read_timeouts_seen;
    unsigned write_calls;
    unsigned write_timeouts_seen;
    unsigned close_calls;
    bool used_after_close; /* A read or write came after close(). */
} ssh_cli_shim_t;

typedef struct {
    char text[CRUX_SSH_FAKE_BUFFER + 1u];
    size_t length;
    bool has_status;
    int status;
    bool eof;
} ssh_cli_result_t;

extern ssh_cli_shim_t ssh_cli_shim;

/* Clears the fake and the shim, starts the fake, and sets the shim as the entry points' provider.
 */
bool ssh_cli_begin(void);
/* A logged-in connection whose attachment holds this session, with its session channel open. */
bool ssh_cli_connect(crux_access_session_t session, int *connection);
/* Queues client input. With end true, EOF follows it, so that a shell's read ends. */
bool ssh_cli_input(int connection, const void *data, size_t length, bool end);
/* Runs a session to its end, with or without a terminal request before it. */
bool ssh_cli_shell(int connection, bool terminal);
bool ssh_cli_exec(int connection, const char *command, size_t length);
ssh_cli_result_t ssh_cli_result(int connection);
/* How often the provider has reported a closed connection since ssh_cli_begin. */
unsigned ssh_cli_connections_closed(void);

#endif /* CRUX_SSH_CLI_HARNESS_H */
