/* The recovery command, as one entry of a command table. It adds no rule of its own: the registry
 * hides it from a session without `network.write`, and the service checks that permission again. */
#include <crux/network_settings.h>
#include <crux/network_settings_cli.h>

#include <string.h>

static crux_cli_result_t say(crux_cli_call_t *call, const char *text)
{
    return crux_cli_write(call, text, strlen(text));
}

static crux_cli_result_t recover(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                 void *context)
{
    (void)argc;
    (void)argv;
    (void)context;
    switch (crux_network_settings_recover(crux_cli_session(call))) {
    case CRUX_NETWORK_OK:
        return say(call, "saved address ignored at the next boot\n");
    case CRUX_NETWORK_ERR_NOT_SUPPORTED:
        return say(call, "nothing can be saved on this device\n");
    case CRUX_NETWORK_ERR_BUSY:
        return say(call, "busy, try again\n");
    case CRUX_NETWORK_ERR_STORE:
        return say(call, "not saved, run the command again\n");
    case CRUX_NETWORK_ERR_DENIED:
    case CRUX_NETWORK_ERR_SESSION_INVALID:
        return CRUX_CLI_ERR_DENIED;
    case CRUX_NETWORK_ERR_INVALID_ARGUMENT:
    case CRUX_NETWORK_ERR_NOT_READY:
    case CRUX_NETWORK_ERR_REJECTED:
    default:
        return CRUX_CLI_ERR_FAILED;
    }
}

static const crux_cli_command_t commands[] = {
    CRUX_CLI_COMMAND("network recover", "ignore the saved address at the next boot",
                     CRUX_CLI_PERMISSION(CRUX_NETWORK_SETTINGS_PERMISSION_WRITE), 0, 0,
                     CRUX_CLI_CHANNEL_SERIAL, recover, NULL),
};

const crux_cli_command_t *crux_network_settings_cli_commands(size_t *count)
{
    if (count != NULL) {
        *count = sizeof(commands) / sizeof(commands[0]);
    }
    return commands;
}
