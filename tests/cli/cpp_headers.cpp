// Every public, adapter and console declaration must also compile for a C++ consumer, and the table
// macro must work without designated initialisers.
#include <crux/cli.h>
#include <crux_cli_adapter.h>
#include <crux_cli_console.h>

static crux_cli_result_t handler(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                 void *context)
{
    (void)call;
    (void)argc;
    (void)argv;
    (void)context;
    return CRUX_CLI_OK;
}

extern const crux_cli_command_t crux_cli_cpp_table[];
const crux_cli_command_t crux_cli_cpp_table[] = {
    CRUX_CLI_COMMAND("one", "a session command", CRUX_CLI_SESSION_ONLY, 0, 1,
                     CRUX_CLI_CHANNEL_SERIAL | CRUX_CLI_CHANNEL_SSH, handler, nullptr),
    CRUX_CLI_COMMAND("two words", "a permission command", CRUX_CLI_PERMISSION("status.read"), 0, 0,
                     CRUX_CLI_CHANNEL_SSH, handler, nullptr),
};
