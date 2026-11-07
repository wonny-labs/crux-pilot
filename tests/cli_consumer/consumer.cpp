#include "consumer.h"

static crux_cli_result_t show(crux_cli_call_t *call, size_t argc, const char *const *argv,
                              void *context)
{
    (void)call;
    (void)argc;
    (void)argv;
    (void)context;
    return CRUX_CLI_OK;
}

extern const crux_cli_command_t cli_consumer_cpp_commands[];
const crux_cli_command_t cli_consumer_cpp_commands[] = {
    CRUX_CLI_COMMAND("app show", "show a value", CRUX_CLI_PERMISSION("app.read"), 0, 1,
                     CRUX_CLI_CHANNEL_SSH, show, nullptr),
};
