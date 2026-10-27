#include "crux_time_command.h"

#include <crux/time.h>

#include <stdio.h>
#include <string.h>

#define INPUT_CAPACITY 256

static void report_result(crux_time_command_result_t result)
{
    if (result.kind == CRUX_TIME_COMMAND_HELP && result.status == CRUX_OK) {
        fputs(crux_time_command_help_text(), stdout);
        fputs("Host simulation uses a controlled clock; it does not follow wall time "
              "or advance automatically.\n",
              stdout);
    } else if (result.status == CRUX_OK) {
        puts(result.text);
    } else {
        puts(crux_time_command_cli_error_text(result.status));
    }
}

int main(void)
{
    char line[INPUT_CAPACITY];

    puts("Crux Time host simulation (controlled clock; no wall-clock progression).");
    if (crux_realtime_init() != CRUX_OK) {
        fputs("ERR IO_ERROR\n", stderr);
        return 1;
    }
    puts("CRUX_TIME_READY composition=host-simulation");

    while (fgets(line, sizeof(line), stdin) != NULL) {
        const size_t length = strlen(line);
        if (length == sizeof(line) - 1 && line[length - 1] != '\n') {
            int character;
            while ((character = getchar()) != '\n' && character != EOF) {
            }
            puts(crux_time_command_cli_error_text(CRUX_ERR_INVALID_ARGUMENT));
            continue;
        }

        char *newline = strchr(line, '\n');
        if (newline != NULL) {
            *newline = '\0';
        }
        report_result(crux_time_command_execute(line));
    }

    return ferror(stdin) ? 1 : 0;
}
