/**
 * @file
 * @brief The serial recovery command of the network settings service, for the command line
 * registry.
 *
 * `network recover` saves the `use_saved` flag as false, so that the next boot uses the
 * composition's default address and not the saved one. It is for a device whose saved address makes
 * it unreachable, and it needs a console on the serial channel, so a composition without one needs
 * another way to recover. The command exists on the serial channel only, in no form on SSH, and it
 * needs `network.write`. Put the table in crux_cli_init() beside the others.
 */
#ifndef CRUX_NETWORK_SETTINGS_CLI_H
#define CRUX_NETWORK_SETTINGS_CLI_H

#include <crux/cli.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The command table, borrowed for the program's life.
 *
 * @param[out] count Number of commands in the table.
 * @return The table, valid for crux_cli_init().
 */
const crux_cli_command_t *crux_network_settings_cli_commands(size_t *count);

#ifdef __cplusplus
}
#endif

#endif
