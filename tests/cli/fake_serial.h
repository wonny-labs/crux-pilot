/* A scripted crux_serial for the console's Host tests. It answers from a fake transport, and its
 * statuses follow the Serial contract: a transport end reads as the chosen status, normally a
 * latched IO, and a timeout reads as CRUX_ERR_TIMEOUT or CRUX_ERR_BUSY. A test composition. */
#ifndef CRUX_CLI_FAKE_SERIAL_H
#define CRUX_CLI_FAKE_SERIAL_H

#include "fake_transport.h"

#include <crux/serial.h>

typedef void (*cli_fake_serial_hook_t)(void);

typedef struct {
    cli_fake_t transport;
    crux_status_t open_result;
    crux_status_t close_result; /* CRUX_ERR_BUSY retains the handle, and others consume it. */
    unsigned close_busy_count;  /* The next closes return CRUX_ERR_BUSY, this many of them. */
    crux_status_t read_end_status;
    crux_status_t write_end_status;
    crux_status_t read_wait_status; /* CRUX_ERR_TIMEOUT or CRUX_ERR_BUSY. */

    unsigned open_calls;
    unsigned close_calls;
    unsigned handles_open;
    crux_serial_port_t last_port;
    crux_serial_config_t last_config;
    cli_fake_serial_hook_t on_open; /* Run first inside each open. */
} cli_fake_serial_t;

extern cli_fake_serial_t cli_fake_serial;

void cli_fake_serial_reset(void);

#endif
