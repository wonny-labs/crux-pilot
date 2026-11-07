/* The console's connection to crux_serial. Portable: the Serial contract owns everything native,
 * and a composition selects the port, which on the EVKB is LPUART3 (rule L1). */
#include "cli_console.h"

#include "cli_internal.h"

#include <crux/thread.h>

typedef struct {
    crux_serial_t serial;
    crux_cli_line_ends_t line_ends;
} console_transport_t;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): crux_cli_io_t fixes this signature.
static int console_read(void *transport, void *buffer, size_t length, uint32_t timeout_ms)
{
    console_transport_t *console = transport;
    size_t transferred = 0;
    crux_status_t status =
        crux_serial_read(console->serial, buffer, length, timeout_ms, &transferred);
    if (status == CRUX_OK) {
        return (int)transferred;
    }
    return status == CRUX_ERR_TIMEOUT || status == CRUX_ERR_BUSY ? 0 : -1;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): crux_cli_io_t fixes this signature.
static int raw_write(void *transport, const void *buffer, size_t length, uint32_t timeout_ms)
{
    console_transport_t *console = transport;
    size_t transferred = 0;
    crux_status_t status =
        crux_serial_write(console->serial, buffer, length, timeout_ms, &transferred);
    if (status == CRUX_OK) {
        return (int)transferred;
    }
    return status == CRUX_ERR_TIMEOUT || status == CRUX_ERR_BUSY ? 0 : -1;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): crux_cli_io_t fixes this signature.
static int console_write(void *transport, const void *buffer, size_t length, uint32_t timeout_ms)
{
    console_transport_t *console = transport;
    return crux_cli_write_terminal(&console->line_ends, raw_write, console, buffer, length,
                                   timeout_ms);
}

static void report_console_error(void)
{
    crux_cli_event_t event = {CRUX_CLI_EVENT_CONSOLE_ERROR,
                              CRUX_CLI_EVENT_FAILED,
                              (uint8_t)CRUX_ACCESS_CHANNEL_SERIAL,
                              {0}};
    crux_cli_port_event(&event);
}

/* Close keeps the handle on BUSY, and consumes it otherwise, even on a native failure. */
static void close_port(crux_cli_console_state_t *state)
{
    crux_status_t status = crux_serial_close(&state->serial);
    if (status != CRUX_ERR_BUSY) {
        state->open = false;
    }
    if (status != CRUX_OK) {
        report_console_error();
    }
}

crux_cli_result_t crux_cli_console_cycle(crux_cli_console_state_t *state,
                                         crux_cli_context_t *context,
                                         const crux_cli_console_config_t *config)
{
    if (state->open) {
        close_port(state);
        if (state->open) {
            return CRUX_CLI_ERR_OUTPUT;
        }
    }
    if (crux_serial_open(config->port, &config->serial, &state->serial) != CRUX_OK) {
        report_console_error();
        return CRUX_CLI_ERR_OUTPUT;
    }
    state->open = true;
    console_transport_t transport = {state->serial, {false}};
    crux_cli_io_t io = {console_read, console_write, &transport, true};
    /* It returns only when the transport has failed, and any session is over by then. */
    crux_cli_result_t result = crux_cli_console_serve(context, &io, config->setup_role);
    report_console_error();
    close_port(state);
    return result;
}

crux_cli_result_t crux_cli_console_run(crux_cli_context_t *context,
                                       const crux_cli_console_config_t *config)
{
    if (context == NULL || config == NULL || config->setup_role == NULL) {
        return CRUX_CLI_ERR_INVALID_ARGUMENT;
    }
    crux_cli_console_state_t state = {{0, 0}, false};
    for (;;) {
        (void)crux_cli_console_cycle(&state, context, config);
        /* A persistent failure retries at a slow rate, with an event for each attempt. */
        (void)crux_thread_sleep_for(crux_duration_from_ms(CRUX_CLI_CONSOLE_RETRY_MS));
    }
}
