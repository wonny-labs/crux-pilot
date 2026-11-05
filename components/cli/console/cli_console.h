/* The console's private parts: the login loop, which is portable and runs over any transport, and
 * one open-serve-close cycle over crux_serial. */
#ifndef CRUX_CLI_CONSOLE_PRIVATE_H
#define CRUX_CLI_CONSOLE_PRIVATE_H

#include "crux_cli_console.h"

/* Pause after a transport or open failure, so that a persistent failure retries at a slow rate. */
#ifndef CRUX_CLI_CONSOLE_RETRY_MS
#define CRUX_CLI_CONSOLE_RETRY_MS 5000
#endif

/* Serves login, setup and sessions until the transport fails. Ends any session before returning.
 * Returns CRUX_CLI_ERR_OUTPUT, or CRUX_CLI_ERR_INVALID_ARGUMENT for a missing argument. */
crux_cli_result_t crux_cli_console_serve(crux_cli_context_t *context, const crux_cli_io_t *io,
                                         const char *setup_role);

/* What survives between cycles: a handle whose close was refused. */
typedef struct {
    crux_serial_t serial;
    bool open;
} crux_cli_console_state_t;

/* One cycle: close a leftover handle, open, serve, and close. Emits cli.console_error for each
 * failure it meets. Returns the result of crux_cli_console_serve, or CRUX_CLI_ERR_OUTPUT if the
 * port would not open. */
crux_cli_result_t crux_cli_console_cycle(crux_cli_console_state_t *state,
                                         crux_cli_context_t *context,
                                         const crux_cli_console_config_t *config);

#endif
