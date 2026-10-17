#ifndef CRUX_TIME_COMMAND_H
#define CRUX_TIME_COMMAND_H

#include <crux/status.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum crux_time_command_kind {
    CRUX_TIME_COMMAND_LINE,
    CRUX_TIME_COMMAND_HELP,
} crux_time_command_kind_t;

typedef struct crux_time_command_result {
    crux_status_t status;
    crux_time_command_kind_t kind;
    char text[32];
} crux_time_command_result_t;

crux_time_command_result_t crux_time_command_execute(const char *input);
const char *crux_time_command_help_text(void);
const char *crux_time_command_startup_banner(void);
const char *crux_time_command_cli_error_text(crux_status_t status);

#ifdef __cplusplus
}
#endif

#endif
