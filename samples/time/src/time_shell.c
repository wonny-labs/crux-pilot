#include "crux_time_command.h"

#include <zephyr/shell/shell.h>

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define COMMAND_CAPACITY 64

static int time_command(const struct shell *shell, size_t argc, char **argv)
{
    char command[COMMAND_CAPACITY] = "time";
    size_t used = sizeof("time") - 1;

    if (argc == 0 || argc > 3 || argv == NULL || argv[0] == NULL || strcmp(argv[0], "time") != 0) {
        shell_print(shell, "ERR INVALID_ARGUMENT");
        return 0;
    }

    for (size_t index = 1; index < argc; ++index) {
        if (argv[index] == NULL) {
            shell_print(shell, "ERR INVALID_ARGUMENT");
            return 0;
        }

        const size_t token_size = strlen(argv[index]);
        if (token_size >= sizeof(command) - used - 1) {
            shell_print(shell, "ERR INVALID_ARGUMENT");
            return 0;
        }

        const int written = snprintf(command + used, sizeof(command) - used, " %s", argv[index]);
        if (written < 0 || (size_t)written >= sizeof(command) - used) {
            shell_print(shell, "ERR INVALID_ARGUMENT");
            return 0;
        }
        used += (size_t)written;
    }

    const crux_time_command_result_t response = crux_time_command_execute(command);
    if (response.kind == CRUX_TIME_COMMAND_HELP && response.status == CRUX_OK) {
        shell_print(shell, "%s", crux_time_command_help_text());
    } else if (response.status == CRUX_OK) {
        shell_print(shell, "%s", response.text);
    } else {
        shell_print(shell, "%s", crux_time_command_cli_error_text(response.status));
    }

    return 0;
}

SHELL_CMD_ARG_REGISTER(time, NULL, "Crux UTC Time commands", time_command, 1,
                       SHELL_OPT_ARG_CHECK_SKIP);
