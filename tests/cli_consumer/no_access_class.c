#include <crux/cli.h>

static crux_cli_result_t handler(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                 void *context)
{
    (void)call;
    (void)argc;
    (void)argv;
    (void)context;
    return CRUX_CLI_OK;
}

extern const crux_cli_command_t cli_no_class_commands[];
const crux_cli_command_t cli_no_class_commands[] = {
    CRUX_CLI_COMMAND("open", "no access class", 0, 0, CRUX_CLI_CHANNEL_SERIAL, handler, NULL),
};
