#include "consumer.h"

#include <string.h>

static crux_cli_result_t show(crux_cli_call_t *call, size_t argc, const char *const *argv,
                              void *context)
{
    (void)argc;
    (void)context;
    if (crux_access_check(crux_cli_session(call), "app.read") != CRUX_ACCESS_OK) {
        return CRUX_CLI_ERR_DENIED;
    }
    return crux_cli_write(call, argv[0], strlen(argv[0]));
}

static const crux_cli_command_t commands[] = {
    CRUX_CLI_COMMAND("app show", "show a value", CRUX_CLI_SESSION_ONLY, 1, 1,
                     CRUX_CLI_CHANNEL_SERIAL | CRUX_CLI_CHANNEL_SSH, show, NULL),
    CRUX_CLI_COMMAND("app recover", "recover from the serial console",
                     CRUX_CLI_PERMISSION("app.write"), 0, 0, CRUX_CLI_CHANNEL_SERIAL, show, NULL),
};

crux_cli_result_t cli_consumer_start(void)
{
    static const crux_cli_command_t *const tables[] = {commands};
    static const size_t counts[] = {sizeof(commands) / sizeof(commands[0])};
    return crux_cli_init(tables, counts, 1);
}
