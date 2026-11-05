/**
 * @file
 * @brief The serial console: login, first-account setup and a session over crux_serial.
 *
 * For the composition that owns the console thread. It is separate from the adapter header so that
 * the SSH adapter never sees a serial type.
 */
#ifndef CRUX_CLI_CONSOLE_H
#define CRUX_CLI_CONSOLE_H

#include <crux/serial.h>
#include <crux_cli_adapter.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The console's settings, as the composition configured them. */
typedef struct {
    crux_serial_port_t port;
    crux_serial_config_t serial;
    const char *setup_role; /**< Role for first-account setup. It must hold access.manage. */
} crux_cli_console_config_t;

/**
 * Serve the serial console: open the port, log in, run a session, and go back to the login prompt.
 *
 * Nothing about the device appears before login. A serial error ends any session, closes and
 * reopens the port, and starts again at the prompt. A port that will not open is retried at a slow
 * rate, so a quarantined Serial binding keeps the console down until a reset, with an event for
 * each attempt. Does not return unless an argument is wrong.
 *
 * @param context The console's memory, used for the program's life.
 * @param config Borrowed for the program's life.
 * @retval CRUX_CLI_ERR_INVALID_ARGUMENT An argument or the setup role is missing.
 */
crux_cli_result_t crux_cli_console_run(crux_cli_context_t *context,
                                       const crux_cli_console_config_t *config);

#ifdef __cplusplus
}
#endif

#endif
