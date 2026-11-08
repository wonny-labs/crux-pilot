#ifndef CRUX_CLI_PORT_H
#define CRUX_CLI_PORT_H

#include <crux/access.h>

#include <stdint.h>

/* The CLI's own events. Access reports its own, including every permission denial. */
typedef enum {
    CRUX_CLI_EVENT_LINE_REJECTED, /* cli.line_rejected. Never carries the line. */
    CRUX_CLI_EVENT_CONSOLE_ERROR  /* cli.console_error */
} crux_cli_event_type_t;

/* Why, in the terms that Issue #182 maps onto its own result values. */
typedef enum {
    CRUX_CLI_EVENT_INVALID, /* Input that the rules refused: an unknown name or a malformed line. */
    CRUX_CLI_EVENT_LIMIT,   /* Input past a declared bound: an overlong line or too many tokens. */
    CRUX_CLI_EVENT_DENIED,  /* A real command this session may not reach. */
    CRUX_CLI_EVENT_FAILED   /* The console's own transport failure. */
} crux_cli_event_outcome_t;

/* Names only a known account: a session's user, or empty. It holds no input. */
typedef struct {
    crux_cli_event_type_t type;
    crux_cli_event_outcome_t outcome;
    uint8_t channel; /* 1 for serial, 3 for SSH. */
    char user[CRUX_ACCESS_NAME_SIZE];
} crux_cli_event_t;

/* Private provider contract, implemented by a test composition or a product provider. Called from
 * a session's thread with no CLI lock held, because the CLI takes none. It must not wait on storage
 * or on a client. */
void crux_cli_port_event(const crux_cli_event_t *event);

#endif
